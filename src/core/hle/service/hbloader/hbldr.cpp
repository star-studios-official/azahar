// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/service/hbloader/hbldr.h"

namespace Service::HBLDR {

HB_LDR::HB_LDR(Core::System& system_)
    : ServiceFramework("hb:ldr", 1), system(system_) {
    // This service exists solely so that svcConnectToPort("hb:ldr") succeeds,
    // which homebrew such as 3HS uses to detect Luma3DS. No IPC commands
    // need to be handled — the connection itself is the detection mechanism.
}

void InstallInterfaces(Core::System& system) {
    std::make_shared<HB_LDR>(system)->InstallAsNamedPort(system.Kernel());
    LOG_INFO(Service_HBLDR, "hb:ldr named port registered (Luma3DS compat stub)");
}

} // namespace Service::HBLDR
