// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/container/unordered_map.h"
#include <unordered_map>
#include <variant>

#include "common/common_types.h"

// fd types?

#include "common/address_space.h"
#include "video_core/host1x/gpu_device_memory_manager.h"
#include "video_core/host1x/syncpoint_manager.h"
#include "video_core/memory_manager.h"

namespace Core {
class System;
} // namespace Core

namespace Tegra::Host1x {

class Host1x {
public:
    explicit Host1x(Core::System& system);
    ~Host1x();

    Core::System& System() {
        return system;
    }

    SyncpointManager& GetSyncpointManager() {
        return syncpoint_manager;
    }

    const SyncpointManager& GetSyncpointManager() const {
        return syncpoint_manager;
    }

    Tegra::MaxwellDeviceMemoryManager& MemoryManager() {
        return memory_manager;
    }

    const Tegra::MaxwellDeviceMemoryManager& MemoryManager() const {
        return memory_manager;
    }

    Common::FlatAllocator<u32, 0, 32>& Allocator() {
        return allocator;
    }

    const Common::FlatAllocator<u32, 0, 32>& Allocator() const {
        return allocator;
    }

    Core::System& system;
    SyncpointManager syncpoint_manager;
    Tegra::MaxwellDeviceMemoryManager memory_manager;
    Tegra::MemoryManager gmmu_manager;
    Common::FlatAllocator<u32, 0, 32> allocator;
};

} // namespace Tegra::Host1x
