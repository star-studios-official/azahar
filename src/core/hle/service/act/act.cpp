// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include <vector>

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/result.h"
#include "core/hle/service/act/act.h"
#include "core/hle/service/act/act_a.h"
#include "core/hle/service/act/act_errors.h"
#include "core/hle/service/act/act_u.h"
#include "core/hle/service/cfg/cfg.h"

SERIALIZE_EXPORT_IMPL(Service::ACT::Module)
SERVICE_CONSTRUCT_IMPL(Service::ACT::Module)

namespace Service::ACT {

namespace {

/// Derives a stable persistent ID from the console unique ID (like the AM module's
/// synthesized device ID), differentiated per account slot so each account gets a
/// unique, non-zero persistent ID.
u32 DerivePersistentId(Core::System& system, u8 slot) {
    const u64 console_id = Service::CFG::GetModule(system)->GetConsoleUniqueId();
    u32 base = static_cast<u32>(console_id & 0xFFFFFFFF);
    if (base == 0) {
        base = 0x53415953; // "SYS" fallback, should never happen
    }
    return ((base & 0x3FFFFFFF) | 0x40000000) + slot * 0x100;
}

} // namespace

Module::Module(Core::System& system_) : system(system_) {
    // Every console has a factory default local account in slot 1 (no NNID linked,
    // friends local account ID 1 = Production). Nimbus relies on this slot existing
    // when it counts accounts before creating a Pretendo account in slot 2.
    auto& default_account = accounts[1];
    default_account.persistent_id = DerivePersistentId(system, 1);
    default_account.fp_local_account_id = 1;
    default_account.committed = true;
}

Module::Interface::Interface(std::shared_ptr<Module> act, const char* name)
    : ServiceFramework(name, 3), act(std::move(act)) {}

Module::Interface::~Interface() = default;

void Module::Interface::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto sdk_version = rp.Pop<u32>();
    const auto shared_memory_size = rp.Pop<u32>();
    const auto caller_pid = rp.PopPID();
    [[maybe_unused]] const auto shared_memory = rp.PopObject<Kernel::SharedMemory>();

    LOG_DEBUG(Service_ACT,
              "(STUBBED) called sdk_version={:08X}, shared_memory_size={:08X}, caller_pid={}",
              sdk_version, shared_memory_size, caller_pid);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::GetErrorCode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto result = rp.Pop<Result>();

    LOG_DEBUG(Service_ACT, "called result={:08X}", result.raw);

    const u32 error_code = GetACTErrorCode(result);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(error_code);
}

void Module::Interface::GetCommonInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto size = rp.Pop<u32>();
    const auto block_id = rp.Pop<u32>();
    auto output_buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_ACT, "called size={:08X}, block_id={:08X}", size, block_id);

    // Zero the whole requested block first so callers reading larger buffers than
    // the block size (e.g. Nimbus reading the u8 account count as a u32) see zero
    // padding, then write the block data.
    const u32 write_size = std::min(size, static_cast<u32>(output_buffer.GetSize()));
    if (write_size > 0) {
        std::vector<u8> data(write_size, 0);
        switch (block_id) {
        case 0x01: // Number of accounts
            data[0] = act->account_count;
            break;
        case 0x02: // Current account slot
            data[0] = act->current_account_slot;
            break;
        case 0x03: // Default account slot
            data[0] = act->default_account_slot;
            break;
        case 0x04: { // NetworkTimeDifference (s64 ns)
            constexpr s64 diff = 0;
            std::memcpy(data.data(), &diff, std::min<size_t>(sizeof(diff), data.size()));
            break;
        }
        case 0x22: // IsApplicationUpdateRequired
            data[0] = 0;
            break;
        default:
            // Everything else (server types, server environment, device hash, ...)
            // is only consumed by NNID/online flows; a zeroed block is fine.
            LOG_DEBUG(Service_ACT, "GetCommonInfo: unhandled block_id={:02X}", block_id);
            break;
        }
        output_buffer.Write(data.data(), 0, write_size);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::GetAccountInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto account_slot = rp.Pop<u8>();
    const auto size = rp.Pop<u32>();
    const auto block_id = rp.Pop<u32>();
    auto output_buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_ACT, "called account_slot={:02X}, size={:08X}, block_id={:08X}",
              account_slot, size, block_id);

    const u32 write_size = std::min(size, static_cast<u32>(output_buffer.GetSize()));
    if (write_size > 0) {
        std::vector<u8> data(write_size, 0);
        const auto& account =
            account_slot <= MaxAccountSlots ? act->accounts[account_slot] : act->accounts[0];
        switch (block_id) {
        case 0x05: { // PersistentId (u32)
            const u32 persistent_id = account.persistent_id;
            std::memcpy(data.data(), &persistent_id,
                        std::min<size_t>(sizeof(persistent_id), data.size()));
            break;
        }
        case 0x08:  // AccountId (NNID): empty on a fresh console
        case 0x0C:  // PrincipalId: none linked
        case 0x11:  // AccountInfo struct: all zero
        case 0x1B:  // Mii name (UTF-16): empty
            break;
        case 0x1A: // IsCommitted (bool)
            data[0] = account.committed ? 1 : 0;
            break;
        case 0x2B: // FpLocalAccountId (u8, friends module local account ID)
            data[0] = account.fp_local_account_id;
            break;
        default:
            // NNID-only blocks (mail address, access token, gender, age, ...) are
            // zeroed: no NNID is linked on this console.
            LOG_DEBUG(Service_ACT, "GetAccountInfo: unhandled block_id={:02X}", block_id);
            break;
        }
        output_buffer.Write(data.data(), 0, write_size);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::CreateConsoleAccount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    Result result = ResultSuccess;
    if (act->account_count >= MaxAccountSlots) {
        result = Result(0xE0E0803C); // No free account slots (ACT::AccountError)
    } else {
        const u8 slot = static_cast<u8>(act->account_count + 1);
        auto& account = act->accounts[slot];
        account = {};
        account.persistent_id = DerivePersistentId(act->system, slot);
        account.fp_local_account_id = slot;
        act->account_count = slot;
        LOG_INFO(Service_ACT, "CreateConsoleAccount: created account in slot {}", slot);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(result);
}

void Module::Interface::CommitConsoleAccount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto account_slot = rp.Pop<u8>();

    if (account_slot >= 1 && account_slot <= act->account_count) {
        act->accounts[account_slot].committed = true;
        LOG_INFO(Service_ACT, "CommitConsoleAccount: committed slot {}", account_slot);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::UnbindServerAccount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto account_slot = rp.Pop<u8>();
    const auto completely = rp.Pop<bool>();

    // There is no NNID data to clear in HLE; the account data is already fresh.
    LOG_INFO(Service_ACT, "UnbindServerAccount: slot={}, completely={}", account_slot, completely);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::UnloadConsoleAccount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    LOG_INFO(Service_ACT, "UnloadConsoleAccount: called");
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::SetDefaultAccount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto account_slot = rp.Pop<u8>();

    if (account_slot >= 1 && account_slot <= act->account_count) {
        act->default_account_slot = account_slot;
        act->current_account_slot = account_slot;
        LOG_INFO(Service_ACT, "SetDefaultAccount: slot {}", account_slot);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

template <class Archive>
void Module::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
}
SERIALIZE_IMPL(Module)

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    auto act = std::make_shared<Module>(system);
    std::make_shared<ACT_A>(act)->InstallAsService(service_manager);
    std::make_shared<ACT_U>(act)->InstallAsService(service_manager);
}

} // namespace Service::ACT
