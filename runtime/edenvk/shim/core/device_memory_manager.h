// SwitchRecomp shim for Eden's Core::DeviceMemoryManager (same interface).
// SwitchRecomp keeps all guest memory in one host block (g_mem + address), so a device address is the guest CPU
// address and the "physical" address is that too: every lookup is g_mem + address. GPU virtual addresses are
// translated by Tegra::MemoryManager, which the glue fills from our nvhost-as-gpu mappings.
#pragma once
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include "common/common_types.h"
#include "common/scratch_buffer.h"
namespace Core {
constexpr size_t DEVICE_PAGEBITS = 12ULL;
constexpr size_t DEVICE_PAGESIZE = 1ULL << DEVICE_PAGEBITS;
constexpr size_t DEVICE_PAGEMASK = DEVICE_PAGESIZE - 1ULL;
class DeviceMemory;
namespace Memory { class Memory; u8* SwrGuestBase(); }
template <typename DTraits> struct DeviceMemoryManagerAllocator {};
struct Asid { size_t id; };
void SwrDeviceWritten(DAddr address, size_t size);   // the GPU side wrote guest memory (not a CPU write)
} // namespace Core
void edenvk_dirty_cached(u64 addr, u64 size, int delta);   // pages Eden keeps (CPU write tracking)
namespace Core {

template <typename Traits>
class DeviceMemoryManager {
    using DeviceInterface = typename Traits::DeviceInterface;
public:
    explicit DeviceMemoryManager(const DeviceMemory&) {}
    DeviceMemoryManager() = default;
    static constexpr bool HAS_FLUSH_INVALIDATION = true;
    static constexpr size_t AS_BITS = Traits::device_virtual_bits;
    void BindInterface(DeviceInterface* d) { device_inter = d; }
    DeviceInterface* Interface() const { return device_inter; }
    DAddr Allocate(size_t) { return 0; }
    void AllocateFixed(DAddr, size_t) {}
    void Free(DAddr, size_t) {}
    void Map(DAddr, VAddr, size_t, Asid, bool = false) {}
    void Unmap(DAddr, size_t) {}
    void TrackContinuityImpl(DAddr, VAddr, size_t, Asid) {}
    void TrackContinuity(DAddr, VAddr, size_t, Asid) {}
    template <typename T> T* GetPointer(DAddr a) { return a && a < (1ULL << AS_BITS) ? reinterpret_cast<T*>(Base() + a) : nullptr; }
    template <typename T> const T* GetPointer(DAddr a) const { return a && a < (1ULL << AS_BITS) ? reinterpret_cast<const T*>(Base() + a) : nullptr; }
    template <typename Func> void ApplyOpOnPAddr(PAddr address, Common::ScratchBuffer<u32>&, Func&& operation) { operation(DAddr(address)); }
    template <typename Func> void ApplyOpOnPointer(const u8* p, Common::ScratchBuffer<u32>& b, Func&& op) { ApplyOpOnPAddr(PAddr(p - Base()), b, op); }
    PAddr GetPhysicalRawAddressFromDAddr(DAddr a) const { return PAddr(a); }
    template <typename T> void Write(DAddr a, T v) { if (T* p = GetPointer<T>(a)) { std::memcpy(p, &v, sizeof(T)); SwrDeviceWritten(a, sizeof(T)); } }
    template <typename T> T Read(DAddr a) const { T v{}; if (const T* p = GetPointer<T>(a)) std::memcpy(&v, p, sizeof(T)); return v; }
    u8* GetSpan(const DAddr a, std::size_t) { return GetPointer<u8>(a); }
    const u8* GetSpan(const DAddr a, std::size_t) const { return GetPointer<u8>(a); }
    void ReadBlock(DAddr a, void* d, size_t n) { if (auto* p = GetPointer<u8>(a)) std::memcpy(d, p, n); else std::memset(d, 0, n); }
    void ReadBlockUnsafe(DAddr a, void* d, size_t n) { ReadBlock(a, d, n); }
    void WriteBlock(DAddr a, const void* s, size_t n) { if (auto* p = GetPointer<u8>(a)) { std::memcpy(p, s, n); SwrDeviceWritten(a, n); } }
    void WriteBlockUnsafe(DAddr a, const void* s, size_t n) { if (auto* p = GetPointer<u8>(a)) std::memcpy(p, s, n); }
    Asid RegisterProcess(Memory::Memory*) { return Asid{0}; }
    void UnregisterProcess(Asid) {}
    void UpdatePagesCachedCount(DAddr a, size_t n, s32 d) { edenvk_dirty_cached(a, n, d); }
    void UpdatePagesCachedBatch(std::span<const std::pair<DAddr, size_t>> r, s32 d) { for (auto& [a, n] : r) edenvk_dirty_cached(a, n, d); }
private:
    static u8* Base() { return Memory::SwrGuestBase(); }
    DeviceInterface* device_inter{};
};
} // namespace Core
