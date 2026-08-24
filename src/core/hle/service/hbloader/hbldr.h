// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::HBLDR {

/// Stub service that registers the "hb:ldr" named port.
/// On real hardware this port is provided by Luma3DS's loader sysmodule.
/// Homebrew such as 3HS probes it via svcConnectToPort to detect Luma3DS;
/// we only need the connection to succeed — no IPC commands are dispatched.
class HB_LDR final : public ServiceFramework<HB_LDR> {
public:
    explicit HB_LDR(Core::System& system_);
    ~HB_LDR() = default;

private:
    Core::System& system;
};

void InstallInterfaces(Core::System& system);

} // namespace Service::HBLDR
