// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::ACT {

/// Initializes all ACT services
class Module final {
public:
    /// Number of usable account slots (1-18 on real hardware).
    static constexpr u32 MaxAccountSlots = 18;

    /// Per-slot account data used by the HLE replacement of the ACT module.
    struct AccountData {
        u32 persistent_id = 0;
        u8 fp_local_account_id = 0;
        bool committed = false;

    private:
        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar& persistent_id;
            ar& fp_local_account_id;
            ar& committed;
        }
        friend class boost::serialization::access;
    };

    explicit Module(Core::System& system_);
    ~Module() = default;

    class Interface : public ServiceFramework<Interface> {
    public:
        Interface(std::shared_ptr<Module> act, const char* name);
        ~Interface();

    protected:
        std::shared_ptr<Module> act;

        /**
         * ACT::Initialize service function.
         * Inputs:
         *     1 : SDK version
         *     2 : Shared Memory Size
         *     3 : PID Translation Header (0x20)
         *     4 : Caller PID
         *     5 : Handle Translation Header (0x0)
         *     6 : Shared Memory Handle
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void Initialize(Kernel::HLERequestContext& ctx);

        /**
         * ACT::GetErrorCode service function.
         * Inputs:
         *     1 : Result code
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         *     2 : Error code
         */
        void GetErrorCode(Kernel::HLERequestContext& ctx);

        /**
         * ACT::GetCommonInfo service function.
         * Inputs:
         *     1 : Size
         *     2 : Block ID (see 3dbrew "DataBlocks" table)
         *     3 : Output Buffer Mapping Translation Header ((Size << 4) | 0xC)
         *     4 : Output Buffer Pointer
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void GetCommonInfo(Kernel::HLERequestContext& ctx);

        /**
         * ACT::GetAccountInfo service function.
         * Inputs:
         *     1 : Account slot
         *     2 : Size
         *     3 : Block ID (see 3dbrew "DataBlocks" table)
         *     4 : Output Buffer Mapping Translation Header ((Size << 4) | 0xC)
         *     5 : Output Buffer Pointer
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void GetAccountInfo(Kernel::HLERequestContext& ctx);

        // act:a only commands (see 3dbrew ACTA:* pages)

        /**
         * ACTA::CreateConsoleAccount service function.
         * Creates a new empty local console account in the next free slot.
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void CreateConsoleAccount(Kernel::HLERequestContext& ctx);

        /**
         * ACTA::CommitConsoleAccount service function.
         * Inputs:
         *     1 : Account slot
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void CommitConsoleAccount(Kernel::HLERequestContext& ctx);

        /**
         * ACTA::UnbindServerAccount service function.
         * Inputs:
         *     1 : Account slot
         *     2 : Completely (clears the assigned account ID / principal ID as well)
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void UnbindServerAccount(Kernel::HLERequestContext& ctx);

        /**
         * ACTA::UnloadConsoleAccount service function.
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void UnloadConsoleAccount(Kernel::HLERequestContext& ctx);

        /**
         * ACTA::SetDefaultAccount service function.
         * Inputs:
         *     1 : Account slot
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void SetDefaultAccount(Kernel::HLERequestContext& ctx);
    };

    /// Account slots indexed 0..MaxAccountSlots; slot 0 is unused.
    std::array<AccountData, MaxAccountSlots + 1> accounts{};
    /// Number of existing accounts. Slot 1 always exists (factory default local account).
    u8 account_count = 1;
    u8 current_account_slot = 1;
    u8 default_account_slot = 1;

private:
    [[maybe_unused]]
    Core::System& system;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

void InstallInterfaces(Core::System& system);

} // namespace Service::ACT

BOOST_CLASS_EXPORT_KEY(Service::ACT::Module)
SERVICE_CONSTRUCT(Service::ACT::Module)