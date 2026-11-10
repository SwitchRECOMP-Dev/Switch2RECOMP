// SwitchRecomp shim: Eden's Core::Memory over SwitchRecomp's flat guest memory (g_mem + address)
#pragma once
#include <cstring>
#include "common/common_types.h"
#include "common/typed_address.h"
namespace Core::Memory {
constexpr u64 YUZU_PAGEBITS = 12;
constexpr u64 YUZU_PAGESIZE = 1ULL << YUZU_PAGEBITS;
constexpr u64 YUZU_PAGEMASK = YUZU_PAGESIZE - 1;
u8* SwrGuestBase();
class Memory {
public:
    u8* GetPointer(Common::ProcessAddress a) { return SwrGuestBase() + GetInteger(a); }
    u8* GetPointerSilent(Common::ProcessAddress a) { return GetPointer(a); }
    template <typename T> T* GetPointer(Common::ProcessAddress a) { return reinterpret_cast<T*>(GetPointer(a)); }
    void ReadBlock(Common::ProcessAddress a, void* d, std::size_t n) { std::memcpy(d, GetPointer(a), n); }
    void ReadBlockUnsafe(Common::ProcessAddress a, void* d, std::size_t n) { ReadBlock(a, d, n); }
    void WriteBlock(Common::ProcessAddress a, const void* s, std::size_t n) { std::memcpy(GetPointer(a), s, n); }
    void WriteBlockUnsafe(Common::ProcessAddress a, const void* s, std::size_t n) { WriteBlock(a, s, n); }
    const u8* GetSpan(Common::ProcessAddress a, std::size_t) { return GetPointer(a); }
    u8* GetSpan(Common::ProcessAddress a, std::size_t, int) { return GetPointer(a); }
    u32 Read32(Common::ProcessAddress a) { u32 v; ReadBlock(a, &v, 4); return v; }
    void RasterizerMarkRegionCached(VAddr, u64, bool) {}
};
} // namespace Core::Memory
