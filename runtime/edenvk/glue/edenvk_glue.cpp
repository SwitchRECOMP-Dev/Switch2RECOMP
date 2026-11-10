/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* renderer=vulkan: the adapter between the SwitchRecomp runtime and Eden's video_core (runtime/edenvk, GPL-3.0).
   Our own GPU front end still reads the pushbuffers, runs the macros (TU104 on Switch 2), schedules Ampere QMDs and
   turns Ampere-only methods into their Maxwell form; every plain engine method then goes to Eden's engines, which
   record the draws and dispatches for its Vulkan rasterizer. Guest memory is one block (g_mem + address), so a device
   address here is the guest CPU address. */
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/frontend/graphics_context.h"
#include "video_core/control/channel_state.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/framebuffer_config.h"
#include "video_core/gpu.h"
#include "video_core/host1x/host1x.h"
#include "video_core/memory_manager.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"

extern "C" {
extern unsigned char* g_mem;
void logf_(int lvl, const char* fmt, ...);
static void swr_evk_exc(const char* fn, const char* what) {
    static int n_;
    if (n_++ < 50) logf_(1, "eden: exception in %s: %s (skipped)\n", fn, what);
}
u8* sm86_lower(const u8* code, u32 max_bytes, int has_sph, u32* out_len, u32* alen, void* inf);
extern int g_sm86_stage;
extern float g_opt_resf;   /* resolution= in the .ini */
extern u32* g_sm86_nmap; extern u32 g_sm86_nnmap;
extern u32 g_sm86_rets[64]; extern u32 g_sm86_nrets;
extern u32 g_sm86_brxb_idx[64], g_sm86_brxb_base[64]; extern u32 g_sm86_nbrxb;
int gmem_read(u64 gva, void* dst, u64 n);
u64 plat_ns(void);
}

/* ---- what the shimmed core/ asks of the runtime */
namespace Core {
namespace Memory {
u8* SwrGuestBase() { return g_mem; }
} // namespace Memory

static System* g_system;

System::System() { g_system = this; }
System::~System() = default;
System& System::GetInstance() { return *g_system; }

namespace Timing {
u64 CoreTiming::GetGPUTicks() const { return plat_ns() * 384 / 625; }   // 614.4 MHz
u64 CoreTiming::GetClockTicks() const { return plat_ns() * 24 / 1250; }  // 19.2 MHz
} // namespace Timing
} // namespace Core

/* CPU write tracking (edenvk_dirty.cpp) */
void edenvk_dirty_scan(const std::function<void(PAddr, size_t)>& cb);
void edenvk_dirty_self(DAddr a, size_t n);
void Core::System::GatherGPUDirtyMemory(std::function<void(PAddr, size_t)>& cb) { edenvk_dirty_scan(cb); }
void Core::SwrDeviceWritten(DAddr a, size_t n) { edenvk_dirty_self(a, n); }

namespace {

class SwrContext final : public Core::Frontend::GraphicsContext {};

class SwrWindow final : public Core::Frontend::EmuWindow {
public:
    SwrWindow(Core::Frontend::WindowSystemType type, void* display, void* surface, u32 w, u32 h) {
        window_info.type = type;
        window_info.display_connection = display;
        window_info.render_surface = surface;
        UpdateCurrentFramebufferLayout(w, h);
    }
    std::unique_ptr<Core::Frontend::GraphicsContext> CreateSharedContext() const override {
        return std::make_unique<SwrContext>();
    }
    bool IsShown() const override { return true; }
    void Resize(u32 w, u32 h) { UpdateCurrentFramebufferLayout(w, h); }
};

struct Chan {
    std::shared_ptr<Tegra::Control::ChannelState> cs;
    int ampere = 0;
    u64 stage_hi[6]{};   // Switch 2: the high bits of each stage's program address
};

struct Glue {
    std::unique_ptr<Core::System> sys;
    std::unique_ptr<SwrWindow> window;
    Tegra::GPU* gpu = nullptr;   // owned by sys
    std::unique_ptr<Tegra::MemoryManager> gmmu;
    std::map<u32, Chan> chans;
    Chan* cur = nullptr;
    u32 cur_id = ~0u;
    std::recursive_mutex lk;
    // lowered Switch 2 programs, by guest address and the hash of their Ampere bytes
    // nmap/rets/brxb: sm86.c's tables for its BRX and RET forms, in shader.c word indices (header counted)
    struct Blob { u64 hash; std::vector<u8> data; int has_sph; int tex_bank = -1; std::vector<u32> nmap, rets; std::vector<std::pair<u32, u32>> brxb; };
    std::unordered_map<const u8*, Blob*> by_data;
    std::unordered_map<u64, Blob> blobs;
    // Eden finds Maxwell's scheduling words at program offsets that are 0 mod 32, which holds for real Maxwell
    // graphics programs (their header starts at 16 mod 32). Lowered Switch 2 programs start 32-aligned, so each stage
    // gets an address moved to 16 mod 32; this maps it back to the real one
    std::unordered_map<u64, u64> moved;
};
Glue* g;

u64 fnv(const u8* p, size_t n) { u64 h = 1469598103934665603ull; for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; } return h; }

Chan& chan(u32 id) {
    auto it = g->chans.find(id);
    if (it == g->chans.end()) {
        Chan c;
        c.cs = g->gpu->AllocateChannel();
        c.cs->memory_manager = g->gmmu.get();
        g->gpu->InitChannel(*c.cs, 0);
        it = g->chans.emplace(id, std::move(c)).first;
        logf_(1, "vulkan: GPU channel %u created\n", id);
    }
    if (g->cur_id != id) {
        g->gpu->BindChannel(it->second.cs->bind_id);
        g->cur_id = id;
        g->cur = &it->second;
    }
    return it->second;
}

} // namespace

extern "C" {

int g_edenvk_on;

/* type: 1 X11 (display, window), 2 Win32 (hwnd). 1 = the renderer is up */
int edenvk_start(int type, void* display, void* surface, u32 w, u32 h) {
    try {
        g = new Glue;
        Settings::values.renderer_backend.SetValue(Settings::RendererBackend::Vulkan);
        Settings::values.use_asynchronous_gpu_emulation.SetValue(false);
        Settings::values.use_disk_shader_cache.SetValue(false);
        if (const char* e = getenv("SWR_EVK_ASTC")) {   // cpu / gpu: how ASTC is decoded when the device has no native ASTC
            Settings::values.accelerate_astc.SetValue(!strcmp(e, "cpu") ? Settings::AstcDecodeMode::Cpu : Settings::AstcDecodeMode::Gpu);
        }
        {   // our internal resolution setting drives Eden's rescaler
            using RS = Settings::ResolutionSetup;
            const float r = g_opt_resf;
            Settings::values.resolution_setup.SetValue(r <= 0.3f ? RS::Res1_4X : r <= 0.6f ? RS::Res1_2X : r <= 0.8f ? RS::Res3_4X : r <= 1.1f ? RS::Res1X :
                                                       r <= 1.6f ? RS::Res3_2X : r <= 2.1f ? RS::Res2X : RS::Res3X);
        }
        Settings::UpdateRescalingInfo();
        Settings::UpdateGPUAccuracy();
        g->sys = std::make_unique<Core::System>();
        g->sys->host1x = std::make_unique<Tegra::Host1x::Host1x>(*g->sys);
        const auto wt = type == 2 ? Core::Frontend::WindowSystemType::Windows : Core::Frontend::WindowSystemType::X11;
        g->window = std::make_unique<SwrWindow>(wt, display, surface, w, h);
        g->sys->gpu = std::make_unique<Tegra::GPU>(*g->sys, false, false);
        g->gpu = g->sys->gpu.get();
        auto ctx = g->window->CreateSharedContext();
        auto renderer = std::make_unique<Vulkan::RendererVulkan>(*g->window, g->sys->Host1x().MemoryManager(), *g->gpu, std::move(ctx));
        g->gpu->BindRenderer(std::move(renderer));
        g->gmmu = std::make_unique<Tegra::MemoryManager>(*g->sys, 40, 1ULL << 40, 16, 12);
        g->gpu->InitAddressSpace(*g->gmmu);
        g_edenvk_on = 1;
        logf_(1, "vulkan: Eden renderer started (%ux%u)\n", w, h);
        return 1;
    } catch (const std::exception& e) {
        logf_(0, "vulkan: the renderer could not start: %s\n", e.what());
    } catch (...) {
        logf_(0, "vulkan: the renderer could not start\n");
    }
    g = nullptr;   // a half-built renderer is left alone: OpenGL takes over
    return 0;
}

void edenvk_resize(u32 w, u32 h) { if (g) g->window->Resize(w, h); }

void edenvk_map(u64 gva, u64 cpu, u64 size) {
    if (!g) return;
    std::scoped_lock l{g->lk};
    g->gmmu->Map(gva, cpu, size, Tegra::PTEKind::PITCH, false);
}

void edenvk_unmap(u64 gva, u64 size) {
    if (!g) return;
    std::scoped_lock l{g->lk};
    g->gmmu->Unmap(gva, size);
}

/* engine: 0 3D, 1 compute, 2 inline-to-memory, 3 2D, 4 DMA */
void edenvk_method(u32 ch, u32 engine, u32 m, u32 v, int last) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    Chan& c = chan(ch);
    auto& p = *c.cs->payload;
    auto& s = *g->sys;
    switch (engine) {
    case 0: p.maxwell_3d.CallMethod(s, m, v, last != 0); break;
    case 1: p.kepler_compute.CallMethod(s, m, v, last != 0); break;
    case 2: p.kepler_memory.CallMethod(s, m, v, last != 0); break;
    case 3: p.fermi_2d.CallMethod(s, m, v, last != 0); break;
    case 4: p.maxwell_dma.CallMethod(s, m, v, last != 0); break;
    }
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

void edenvk_set_ampere(u32 ch) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    chan(ch).ampere = 1;
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

/* Switch 2 stage program address (SET_PIPELINE_PROGRAM_ADDRESS_A/B): Eden gets offset = low 32 bits and this base */
void edenvk_stage_addr(u32 ch, u32 stage, u64 addr) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    Chan& c = chan(ch);
    if (stage < 6) c.stage_hi[stage] = addr & ~0xFFFFFFFFull;
    const u32 off = static_cast<u32>(addr);
    const u32 moved = off + ((16 - (off & 31)) & 31);
    if (moved != off) g->moved[(addr & ~0xFFFFFFFFull) + moved] = addr;
    c.cs->payload->maxwell_3d.CallMethod(*g->sys, 0x801 + 16 * stage, moved, true);
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

/* Switch 2 DRAW_*_BEGIN_END */
void edenvk_draw(u32 ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex, u32 base_instance, u32 instances) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    Chan& c = chan(ch);
    auto& m3d = c.cs->payload->maxwell_3d;
    const auto topo = static_cast<Tegra::Engines::Maxwell3D::Regs::PrimitiveTopology>(topology);
    if (indexed)
        m3d.draw_manager.DrawIndex(m3d, topo, first, count, base_vertex, base_instance, instances ? instances : 1);
    else
        m3d.draw_manager.DrawArray(m3d, topo, first, count, base_instance, instances ? instances : 1);
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

/* Switch 2 compute: our front end converted the QMD 3.0 to the 2.x layout and gives the program's code base */
void edenvk_compute(u32 ch, const u32* qmd2, u64 code_base) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    if (!code_base && !qmd2[8]) return;   // NVN's fence QMDs have no program, only releases (our front end writes them)
    Chan& c = chan(ch);
    c.ampere = 1;   // only Switch 2 launches come this way (a compute-only channel never sends 3D methods)
    auto& kc = c.cs->payload->kepler_compute;
    kc.CallMethod(*g->sys, 0x582, static_cast<u32>(code_base >> 32), true);
    kc.CallMethod(*g->sys, 0x583, static_cast<u32>(code_base), true);
    // the program start moved to 0 mod 32 (Eden's scheduling-word rule for compute), mapped back for the code read
    u32 q[64];
    std::memcpy(q, qmd2, sizeof q);
    const u32 moved = q[8] + ((32 - (q[8] & 31)) & 31);
    if (moved != q[8]) g->moved[code_base + moved] = code_base + q[8];
    q[8] = moved;
    kc.SwrLaunch(q);
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

/* the end of a GPFIFO submission: CPU writes since the last one are invalidated first by the next draw (the
   rasterizer asks GatherGPUDirtyMemory), commands go to the host GPU, finished fences write their memory */
void edenvk_submit_begin(void) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    if (!g) return;
    g->gpu->InvalidateGPUCache();
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

void edenvk_submit_end(void) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    if (!g) return;
    auto* r = g->gpu->Renderer().ReadRasterizer();
    // query and semaphore writes wait in Eden for the next fence (its puller would make one on a syncpoint or
    // semaphore method, which our front end handles): make it here and wait, so the memory is written before our
    // runtime increments the submit's syncpoint
    r->SignalReference();
    r->FlushCommands();
    r->ReleaseFences(true);
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

/* everything the GPU wrote back to guest memory before the CPU reads it (syncpoint waits, readbacks) */
void edenvk_sync(void) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    if (!g) return;
    auto* r = g->gpu->Renderer().ReadRasterizer();
    r->FlushCommands();
    r->ReleaseFences(true);
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

void edenvk_present(u64 addr, u32 w, u32 h, u32 pitch, u32 layout, u32 bhl, u32 fmt, const int crop[4]) {
    if (!g) return;
    std::scoped_lock lock_{g->lk};   // Eden isn't thread safe: the GPU thread, vi and nvdrv (map/unmap) all come here
    try {
    if (!g) return;
    (void)layout; (void)bhl;
    Tegra::FramebufferConfig f{};
    f.address = addr;
    f.offset = 0;
    f.width = w;
    f.height = h;
    f.stride = pitch >= w * 4 ? pitch / 4 : w;   // in pixels (nvnflinger's stride); we get bytes
    // Android formats 1..7 (vi); Switch 2's vi2 hands NVN format codes (152 = RGBA8): 4 bytes a pixel like our OpenGL path
    f.pixel_format = static_cast<Service::android::PixelFormat>(fmt >= 1 && fmt <= 7 ? fmt : 1);
    { static int n; if (n < 6) { n++; logf_(1, "vulkan: present %llx %ux%u pitch %u format %u layout %u\n", (unsigned long long)addr, w, h, pitch, fmt, layout); } }
    f.transform_flags = Service::android::BufferTransformFlags::Unset;
    if (crop && (crop[2] > crop[0]) && (crop[3] > crop[1])) f.crop_rect = Common::Rectangle<int>(crop[0], crop[1], crop[2], crop[3]);
    f.blending = Tegra::BlendMode::Opaque;
    std::vector<Tegra::FramebufferConfig> layers{f};
    { // SWR_EVK_SHOT=N: what the renderer itself made at present N goes to evk_shot.ppm (the window may not show it under Xvfb)
        static int want = -2, n;
        if (want == -2) { const char* e = getenv("SWR_EVK_SHOT"); want = e ? atoi(e) : -1; }
        if (want >= 0 && ++n == want) {
            static std::vector<u8> px;
            const auto lay = getenv("SWR_EVK_SHOT_FULL") ? Layout::DefaultFrameLayout(w, h) : g->window->GetFramebufferLayout();
            px.assign(static_cast<size_t>(lay.width) * lay.height * 4, 0);
            const u32 sw = lay.width, sh = lay.height;
            g->gpu->Renderer().RequestScreenshot(px.data(), [sw, sh](bool) {
                if (FILE* fo = fopen("evk_shot.ppm", "wb")) {
                    fprintf(fo, "P6\n%u %u\n255\n", sw, sh);
                    for (size_t i = 0; i < static_cast<size_t>(sw) * sh; ++i) { const u8 rgb[3]{px[i * 4 + 2], px[i * 4 + 1], px[i * 4]}; fwrite(rgb, 1, 3, fo); }   // BGRA
                    fclose(fo);
                }
                logf_(1, "vulkan: renderer screenshot saved (evk_shot.ppm)\n");
            }, lay);
        }
    }
    g->gpu->Renderer().Composite(layers);
    g->gpu->RendererFrameEndNotify();   // Composite already ticks the rasterizer's frame (caches, garbage collection)
    } catch (const std::exception& ex_) { swr_evk_exc(__func__, ex_.what()); } catch (...) { swr_evk_exc(__func__, "?"); }
}

/* Eden's shader environment: the Maxwell program sm86.c makes from an Ampere one (0 = read the guest code as is) */
int edenvk_shader_blob(u64 gpu_addr, int has_sph, const u8** data, size_t* size) {
    if (!g || !g->cur || !g->cur->ampere) { static int n; if (g && n < 10) { n++; logf_(1, "vulkan: program at %llx read as Maxwell (channel %u)\n", (unsigned long long)gpu_addr, g->cur_id); } return 0; }
    { auto it = g->moved.find(gpu_addr); if (it != g->moved.end()) gpu_addr = it->second; }
    const std::optional<DAddr> cpu = g->gmmu->GpuToCpuAddress(gpu_addr);
    if (!cpu) return 0;
    const u32 cap = 0x40000;
    std::vector<u8> code(cap);
    g->gmmu->ReadBlock(gpu_addr, code.data(), cap);
    u32 ol = 0, al = 0;
    unsigned char info[128]{};   // Sm86Info
    g_sm86_stage = 0;
    u8* low = sm86_lower(code.data(), cap, has_sph, &ol, &al, &info);
    if (!low) { logf_(0, "vulkan: Switch 2 program at %llx could not be lowered\n", (unsigned long long)gpu_addr); return 0; }
    const u64 h = fnv(code.data(), al ? al : cap);
    auto& b = g->blobs[*cpu];
    if (b.data.empty() || b.hash != h) {
        b.hash = h;
        b.data.assign(low, low + ol);
        b.has_sph = has_sph;
        { int tb; std::memcpy(&tb, info + 24, 4); b.tex_bank = tb; }   // Sm86Info.tex_bank
        b.nmap.assign(g_sm86_nmap ? g_sm86_nmap : nullptr, g_sm86_nmap ? g_sm86_nmap + g_sm86_nnmap : nullptr);
        b.rets.assign(g_sm86_rets, g_sm86_rets + (g_sm86_nrets < 64 ? g_sm86_nrets : 64));
        b.brxb.clear();
        for (u32 q = 0; q < g_sm86_nbrxb && q < 64; ++q) b.brxb.emplace_back(g_sm86_brxb_idx[q], g_sm86_brxb_base[q]);
        // sm86.c leaves 0 where an Ampere instruction needs nothing (our shader.c skips those): Eden wants a NOP there
        const size_t first = has_sph ? 10 : 0;
        u64* w = reinterpret_cast<u64*>(b.data.data());
        for (size_t i = first; i < b.data.size() / 8; ++i)
            if (((i - first) & 3) != 0 && w[i] == 0) w[i] = 0x50B0000000070F00ull;
    }
    free(low);
    { static int n; if (n < 40) { n++; logf_(1, "vulkan: Switch 2 %s program at %llx (cpu %llx): %u bytes, lowered %u\n", has_sph ? "graphics" : "compute",
        (unsigned long long)gpu_addr, (unsigned long long)*cpu, al, ol); } }
    if (getenv("SWR_EVK_DUMP")) { char fp[128]; snprintf(fp, sizeof fp, "evkdump/%llx_%s.bin", (unsigned long long)*cpu, has_sph ? "gfx" : "cs");
        if (FILE* f = fopen(fp, "wb")) { fwrite(b.data.data(), 1, b.data.size(), f); fclose(f); }
        logf_(1, "vulkan: blob %s\n", fp); }
    g->by_data[b.data.data()] = &b;
    *data = b.data.data();
    *size = b.data.size();
    return 1;
}

/* sm86.c's indirect branches for Eden's CFG. brx_word: the BRX's word in the blob. keys = what the register holds
   (Ampere byte offsets), targets = byte offsets in the blob */
int edenvk_brx(const u8* blob, u32 brx_word, int is_ret, u32 (*rd)(void*, u32, u32), void* ctx, u32* keys, u32* targets, int max) {
    if (!g) return -1;
    auto it = g->by_data.find(blob);
    if (it == g->by_data.end()) return -1;
    const Glue::Blob& b = *it->second;
    const u32 hdr = b.has_sph ? 0 : 10;   // shader.c counts the 10 header words; compute blobs have none
    const u32 nwords = static_cast<u32>(b.data.size() / 8) + hdr;
    auto to_blob = [&](u32 t, u32* out) { if (t < 11 || t >= nwords) return false; *out = (t - hdr) * 8; return true; };
    int n = 0;
    if (is_ret) {
        for (u32 ai : b.rets) {
            if (ai >= b.nmap.size() || n >= max) continue;
            if (to_blob(b.nmap[ai], &targets[n])) { keys[n] = ai * 16; ++n; }
        }
        return n;
    }
    const u64* w = reinterpret_cast<const u64*>(b.data.data());
    const u32 i = brx_word + hdr;   // shader.c index
    const u32 ra = static_cast<u32>((w[brx_word] >> 8) & 0xFF);
    u32 base = 0;
    for (auto& [idx, bs] : b.brxb) if (idx == i) { base = bs; break; }
    for (u32 k = 1; k <= 64 && k < i; ++k) {   // the table load, as shader.c looks for it
        const u32 j = i - k;
        if (j < 11 || ((j - 10) & 3) == 0) continue;
        const u64 x = w[j - hdr];
        const bool ldc = (x >> 51) == 0x1DF2;   // LDC: 1110 1111 1001 0
        if (!ldc) { if (((x >> 0) & 0xFF) == ra) return -1; continue; }
        if ((x & 0xFF) != ra) continue;
        const u32 cidx = static_cast<u32>((x >> 36) & 31);
        const s32 coff = static_cast<s32>(static_cast<s16>((x >> 20) & 0xFFFF));
        for (u32 e = 0; e < 64 && n < max; ++e) {
            const u32 v = rd(ctx, cidx, static_cast<u32>(coff + static_cast<s32>(e) * 4));
            const u32 a = v + base;
            if ((a & 15) || a / 16 >= b.nmap.size() || (e && !v)) break;
            if (!to_blob(b.nmap[a / 16], &targets[n])) break;
            keys[n] = v;
            ++n;
        }
        return n ? n : -1;
    }
    return -1;
}

int edenvk_blob_texbank(const u8* blob) {
    if (!g) return -1;
    auto it = g->by_data.find(blob);
    return it == g->by_data.end() ? -1 : it->second->tex_bank;
}

u64 edenvk_stage_base(int stage, u64 base_addr) {
    if (!g || !g->cur || !g->cur->ampere || stage < 0 || stage >= 6) return base_addr;
    return g->cur->stage_hi[stage];
}

} // extern "C"
