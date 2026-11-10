/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* CPU write tracking for renderer=vulkan. The recompiled code stores straight into guest memory, so nothing tells
   Eden's caches that a buffer or texture changed. Eden says which pages it keeps (UpdatePagesCachedCount); before a
   submission, the pages among those the CPU wrote since the last look are handed to the rasterizer to invalidate.
   Windows: GetWriteWatch on guest memory reserved with MEM_WRITE_WATCH (it resets what it reports).
   Linux: the kernel's soft-dirty bits (/proc/self/pagemap bit 55, cleared through /proc/self/clear_refs). */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include "common/common_types.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

extern "C" {
extern unsigned char* g_mem;
void logf_(int lvl, const char* fmt, ...);
extern int g_ww_ok;
}

namespace {
constexpr u64 PAGE_BITS = 12;
constexpr u64 AS_PAGES = 0x2000000000ull >> PAGE_BITS;   // AS_SIZE
std::mutex lk;
u8* counts;                         // per page: how many cached objects hold it (calloc'd, untouched pages cost nothing)
std::vector<u64> runs_first, runs_last;   // rebuilt lazily from counts
bool runs_dirty = true;
std::unordered_set<u64> self_pages;  // pages Eden itself wrote since the last scan
std::unordered_set<u64> cached_set;  // pages with a nonzero count (so a scan doesn't walk 32M counters)
#ifndef _WIN32
int pagemap_fd = -1;
int clear_fd = -1;
#endif

void ensure() {
    if (!counts) counts = static_cast<u8*>(calloc(AS_PAGES, 1));
#ifndef _WIN32
    if (pagemap_fd < 0) pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    if (clear_fd < 0) clear_fd = open("/proc/self/clear_refs", O_WRONLY);
#endif
}
} // namespace

void edenvk_dirty_cached(u64 addr, u64 size, int delta) {
    if (!size) return;
    std::scoped_lock l{lk};
    ensure();
    if (!counts) return;
    const u64 first = addr >> PAGE_BITS, last = (addr + size - 1) >> PAGE_BITS;
    for (u64 p = first; p <= last && p < AS_PAGES; ++p) {
        const int v = counts[p] + delta;
        const u8 nv = static_cast<u8>(v < 0 ? 0 : v > 255 ? 255 : v);
        if (!counts[p] && nv) cached_set.insert(p);
        else if (counts[p] && !nv) cached_set.erase(p);
        counts[p] = nv;
    }
    runs_dirty = true;
}

void edenvk_dirty_self(u64 addr, u64 size) {
    if (!size) return;
    std::scoped_lock l{lk};
    for (u64 p = addr >> PAGE_BITS; p <= (addr + size - 1) >> PAGE_BITS; ++p) self_pages.insert(p);
}

static void build_runs() {
    if (!runs_dirty) return;
    runs_dirty = false;
    std::vector<u64> pages(cached_set.begin(), cached_set.end());
    std::sort(pages.begin(), pages.end());
    runs_first.clear();
    runs_last.clear();
    for (u64 p : pages) {
        if (!runs_last.empty() && runs_last.back() + 1 == p) runs_last.back() = p;
        else { runs_first.push_back(p); runs_last.push_back(p); }
    }
}

void edenvk_dirty_scan(const std::function<void(PAddr, size_t)>& cb) {
    std::vector<std::pair<u64, u64>> out;   // page ranges written
    {
        std::scoped_lock l{lk};
        ensure();
        build_runs();
        auto emit = [&](u64 p) {
            if (self_pages.count(p)) return;
            if (!out.empty() && out.back().second == p) out.back().second = p + 1;
            else out.emplace_back(p, p + 1);
        };
#ifdef _WIN32
        if (!g_ww_ok) return;
        static std::vector<PVOID> hits(65536);
        for (size_t r = 0; r < runs_first.size(); ++r) {
            u8* base = g_mem + (runs_first[r] << PAGE_BITS);
            const SIZE_T len = (runs_last[r] - runs_first[r] + 1) << PAGE_BITS;
            for (;;) {
                ULONG_PTR n = hits.size();
                ULONG gran = 0;
                if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, base, len, hits.data(), &n, &gran)) break;
                for (ULONG_PTR i = 0; i < n; ++i) emit(static_cast<u64>(static_cast<u8*>(hits[i]) - g_mem) >> PAGE_BITS);
                if (n < hits.size()) break;
            }
        }
#else
        if (pagemap_fd < 0 || clear_fd < 0) return;
        std::vector<u64> e;
        for (size_t r = 0; r < runs_first.size(); ++r) {
            const u64 hp0 = (reinterpret_cast<u64>(g_mem) >> PAGE_BITS) + runs_first[r];
            const u64 n = runs_last[r] - runs_first[r] + 1;
            e.resize(n);
            if (pread(pagemap_fd, e.data(), n * 8, static_cast<off_t>(hp0 * 8)) != static_cast<ssize_t>(n * 8)) continue;
            for (u64 i = 0; i < n; ++i)
                if (e[i] & (1ull << 55)) emit(runs_first[r] + i);
        }
        if (write(clear_fd, "4", 1) != 1) {}
#endif
        self_pages.clear();
    }
    for (auto& [a, b] : out) cb(a << PAGE_BITS, (b - a) << PAGE_BITS);
}
