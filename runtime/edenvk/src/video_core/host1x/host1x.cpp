// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/core.h"
#include "video_core/host1x/host1x.h"

namespace Tegra::Host1x {

Host1x::Host1x(Core::System& system_)
    : system{system_}
    , syncpoint_manager{}
    , memory_manager(system.DeviceMemory())
    , gmmu_manager{system, memory_manager, 40, 0, 12}
    , allocator{1 << 12}
{}

Host1x::~Host1x() = default;

} // namespace Tegra::Host1x
