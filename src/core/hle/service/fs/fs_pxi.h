// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <unordered_map>
#include "common/common_types.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/fs/directory.h"
#include "core/hle/service/fs/file.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::FS {

class ArchiveManager;

/**
 * ARM9 filesystem service ("PxiFS0") exposed over PXI. On real hardware it is only reachable by
 * system modules through the PXI bus, but CFW-style homebrew (e.g. PKSM) steals a client session
 * to it via the Luma3DS custom ControlService SVC (0xB0) and then issues FSPXI commands directly
 * on that session. Commands use PXI buffer descriptors, which the kernel passes through unchanged.
 */
class FS_PXI final : public ServiceFramework<FS_PXI> {
public:
    explicit FS_PXI(Core::System& system);
    ~FS_PXI();

private:
    void OpenArchive(Kernel::HLERequestContext& ctx);
    void CloseArchive(Kernel::HLERequestContext& ctx);
    void CommitSaveData(Kernel::HLERequestContext& ctx);

    void OpenFile(Kernel::HLERequestContext& ctx);
    void CloseFile(Kernel::HLERequestContext& ctx);
    void ReadFile(Kernel::HLERequestContext& ctx);
    void WriteFile(Kernel::HLERequestContext& ctx);
    void GetFileSize(Kernel::HLERequestContext& ctx);
    void SetFileSize(Kernel::HLERequestContext& ctx);
    void CreateFile(Kernel::HLERequestContext& ctx);
    void DeleteFile(Kernel::HLERequestContext& ctx);
    void RenameFile(Kernel::HLERequestContext& ctx);

    void OpenDirectory(Kernel::HLERequestContext& ctx);
    void CloseDirectory(Kernel::HLERequestContext& ctx);
    void ReadDirectory(Kernel::HLERequestContext& ctx);
    void CreateDirectory(Kernel::HLERequestContext& ctx);
    void DeleteDirectory(Kernel::HLERequestContext& ctx);
    void RenameDirectory(Kernel::HLERequestContext& ctx);

    void CalcSavegameMAC(Kernel::HLERequestContext& ctx);

    void GetCardType(Kernel::HLERequestContext& ctx);
    void GetSdmcArchiveResource(Kernel::HLERequestContext& ctx);
    void GetNandArchiveResource(Kernel::HLERequestContext& ctx);
    void IsSdmcDetected(Kernel::HLERequestContext& ctx);
    void IsSdmcWritable(Kernel::HLERequestContext& ctx);
    void CardSlotIsInserted(Kernel::HLERequestContext& ctx);

    /// Returns the program ID of the client process that issued the request.
    u64 GetClientProgramId(Kernel::HLERequestContext& ctx) const;

    Core::System& system;
    ArchiveManager& archives;

    /// FSPXI file/directory handles are integer values returned inline in the reply; they only
    /// need to stay valid for the lifetime of the file/directory. They are not shared with any
    /// other service, unlike fs:USER which returns session handles.
    u64 next_file_handle = 1;
    u64 next_dir_handle = 1;
    std::unordered_map<u64, std::shared_ptr<File>> file_handles;
    std::unordered_map<u64, std::shared_ptr<Directory>> dir_handles;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

} // namespace Service::FS

SERVICE_CONSTRUCT(Service::FS::FS_PXI)
BOOST_CLASS_EXPORT_KEY(Service::FS::FS_PXI)
