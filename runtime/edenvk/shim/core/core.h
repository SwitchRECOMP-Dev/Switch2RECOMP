// SwitchRecomp shim for Eden's Core::System: only what video_core asks of it.
#pragma once
#include <functional>
#include <memory>
#include "common/common_types.h"
#include "core/core_timing.h"
#include "core/memory.h"
#include "core/perf_stats.h"
namespace Tegra { class GPU; namespace Host1x { class Host1x; } }
namespace Core {
class DeviceMemory {};
class System {
public:
    System();
    ~System();
    static System& GetInstance();
    Tegra::GPU& GPU() { return *gpu; }
    Tegra::Host1x::Host1x& Host1x() { return *host1x; }
    Timing::CoreTiming& CoreTiming() { return timing; }
    PerfStats& GetPerfStats() { return perf; }
    DeviceMemory& DeviceMemory() { return devmem; }
    Memory::Memory& ApplicationMemory() { return mem; }
    bool IsPoweredOn() const { return true; }
    void RegisterHostThread() {}
    // CPU writes since the last call (the glue scans write tracking), each range given to the callback
    void GatherGPUDirtyMemory(std::function<void(PAddr, size_t)>& callback);
    std::unique_ptr<Tegra::GPU> gpu;
    std::unique_ptr<Tegra::Host1x::Host1x> host1x;
private:
    Timing::CoreTiming timing;
    PerfStats perf;
    class DeviceMemory devmem;
    Memory::Memory mem;
};
} // namespace Core
