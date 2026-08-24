// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "core/hle/service/service.h"

namespace Kernel {
class Event;
class SharedMemory;
} // namespace Kernel

namespace Service::NWM {

class NWM_SOC final : public ServiceFramework<NWM_SOC> {
public:
    explicit NWM_SOC(Core::System& system);

private:
    void GetMACAddress(Kernel::HLERequestContext& ctx);
    void GetMbufPoolInformation(Kernel::HLERequestContext& ctx);

    Core::System& system;

    // Shared memory for mbuf pool (0x22000 bytes total, 0x40 entries of 0x800 bytes each)
    std::shared_ptr<Kernel::SharedMemory> mbuf_shared_mem;
    std::shared_ptr<Kernel::Event> mbuf_event;

    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::NWM

SERVICE_CONSTRUCT(Service::NWM::NWM_SOC)
BOOST_CLASS_EXPORT_KEY(Service::NWM::NWM_SOC)
