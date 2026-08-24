// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/archives.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/cfg/cfg_u.h"
#include "core/hle/service/nwm/nwm_soc.h"
#include "core/hle/service/nwm/nwm_uds.h"

SERIALIZE_EXPORT_IMPL(Service::NWM::NWM_SOC)
SERVICE_CONSTRUCT_IMPL(Service::NWM::NWM_SOC)

namespace Service::NWM {

/// NWM mbuf pool shared memory total size (0x40 entries x 0x800 bytes each + metadata)
static constexpr u32 MbufPoolSize = 0x22000;

void NWM_SOC::GetMACAddress(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 size = rp.Pop<u32>();

    std::vector<u8> mac_buffer(size);
    MacAddress mac;

    if (auto cfg = system.ServiceManager().GetService<Service::CFG::CFG_U>("cfg:u")) {
        auto cfg_module = cfg->GetModule();
        mac = Service::CFG::MacToArray(cfg_module->GetMacAddress());
    }

    std::copy(mac.begin(), mac.begin() + std::min(mac.size(), mac_buffer.size()),
              mac_buffer.begin());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushStaticBuffer(mac_buffer, 0);
}

void NWM_SOC::GetMbufPoolInformation(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_NWM, "called");

    // Create shared memory and event on first call (lazy initialization)
    if (!mbuf_shared_mem) {
        auto process = system.Kernel().GetCurrentProcess();
        auto result = system.Kernel().CreateSharedMemory(
            process, MbufPoolSize, Kernel::MemoryPermission::ReadWrite,
            Kernel::MemoryPermission::ReadWrite, 0, Kernel::MemoryRegion::SYSTEM,
            "nwm::SOC mbuf pool");
        if (!result.Succeeded()) {
            LOG_ERROR(Service_NWM, "Failed to create mbuf pool shared memory");
            IPC::RequestParser rp(ctx);
            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push(ResultUnknown);
            return;
        }

        mbuf_shared_mem = std::move(result).Unwrap();

        // Zero-initialize the shared memory
        auto* mem = mbuf_shared_mem->GetPointer(0);
        if (mem) {
            std::memset(mem, 0, MbufPoolSize);
        }
    }

    if (!mbuf_event) {
        mbuf_event = system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "nwm::SOC mbuf event");
        if (!mbuf_event) {
            LOG_ERROR(Service_NWM, "Failed to create mbuf event");
            IPC::RequestParser rp(ctx);
            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push(ResultUnknown);
            return;
        }
    }

    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(5, 0);
    rb.Push(ResultSuccess);
    rb.Push<u32>(MbufPoolSize); // sharedmem_size at cmdreply[2]
    rb.PushCopyObjects(mbuf_shared_mem, mbuf_event);
    // cmdreply[4] = sharedmem_handle, cmdreply[5] = eventhandle
}

NWM_SOC::NWM_SOC(Core::System& _system) : ServiceFramework("nwm::SOC"), system(_system) {

    static const FunctionInfo functions[] = {
        {0x0001, nullptr, "SetSharedMem"},
        {0x0002, nullptr, "GetSharedMem"},
        {0x0003, nullptr, "SetSharedMem2"},
        {0x0004, nullptr, "WriteSharedMem"},
        {0x0005, nullptr, "Unknown5"},
        {0x0006, nullptr, "Unknown6"},
        {0x0007, nullptr, "Unknown7"},
        {0x0008, &NWM_SOC::GetMACAddress, "GetMACAddress"},
        {0x0009, &NWM_SOC::GetMbufPoolInformation, "GetMbufPoolInformation"},
        {0x000A, nullptr, "UnknownA"},
        {0x000B, nullptr, "UnknownB"},
        {0x000C, nullptr, "UnknownC"},
        {0x000D, nullptr, "UnknownD"},
    };
    RegisterHandlers(functions);
}

} // namespace Service::NWM
