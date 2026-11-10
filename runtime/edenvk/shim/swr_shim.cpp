// SwitchRecomp: what the copied Eden sources expect from parts of Eden that aren't built here.
#include <array>
#include <string>
#include "common/time_zone.h"
#define VMA_IMPLEMENTATION
#include "video_core/vulkan_common/vma.h"

namespace Common::TimeZone {
const std::array<const char*, 46>& GetTimeZoneStrings() { static std::array<const char*, 46> z{}; return z; }
std::string FindSystemTimeZone() { return "UTC"; }
} // namespace Common::TimeZone
