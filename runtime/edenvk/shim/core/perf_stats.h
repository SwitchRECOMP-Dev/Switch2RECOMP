// SwitchRecomp shim for Eden's Core::PerfStats
#pragma once
namespace Core {
class PerfStats { public: void EndGameFrame() {} };
} // namespace Core
