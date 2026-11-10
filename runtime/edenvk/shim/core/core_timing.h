// SwitchRecomp shim for Eden's Core::Timing (GPL-3.0-or-later, see runtime/edenvk/README.txt)
#pragma once
#include <chrono>
#include "common/common_types.h"
namespace Core::Timing {
class CoreTiming {
public:
    u64 GetGPUTicks() const;   // Maxwell GPU clock (614.4 MHz) from the host clock
    u64 GetClockTicks() const;
};
} // namespace Core::Timing
