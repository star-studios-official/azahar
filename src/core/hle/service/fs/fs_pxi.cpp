// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <vector>
#include <boost/serialization/base_object.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/cmac.h>
#include <cryptopp/sha.h>
#include "common/archives.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/directory_backend.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/file_backend.h"
#include "core/file_sys/nds_rom.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/fs/fs_pxi.h"

SERVICE_CONSTRUCT_IMPL(Service::FS::FS_PXI)
SERIALIZE_EXPORT_IMPL(Service::FS::FS_PXI)

namespace Service::FS {

FS_PXI::FS_PXI(Core::System& system)
    : ServiceFramework("PxiFS0", 8), system(system), archives(system.ArchiveManager()) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &FS_PXI::OpenFile, "OpenFile"},
        {0x0002, &FS_PXI::DeleteFile, "DeleteFile"},
        {0x0003, &FS_PXI::RenameFile, "RenameFile"},
        {0x0004, &FS_PXI::DeleteDirectory, "DeleteDirectory"},
        {0x0005, &FS_PXI::CreateFile, "CreateFile"},
        {0x0006, &FS_PXI::CreateDirectory, "CreateDirectory"},
        {0x0007, &FS_PXI::RenameDirectory, "RenameDirectory"},
        {0x0008, &FS_PXI::OpenDirectory, "OpenDirectory"},
        {0x0009, &FS_PXI::ReadFile, "ReadFile"},
        {0x000B, &FS_PXI::WriteFile, "WriteFile"},
        {0x000C, &FS_PXI::CalcSavegameMAC, "CalcSavegameMAC"},
        {0x000D, &FS_PXI::GetFileSize, "GetFileSize"},
        {0x000E, &FS_PXI::SetFileSize, "SetFileSize"},
        {0x000F, &FS_PXI::CloseFile, "CloseFile"},
        {0x0010, &FS_PXI::ReadDirectory, "ReadDirectory"},
        {0x0011, &FS_PXI::CloseDirectory, "CloseDirectory"},
        {0x0012, &FS_PXI::OpenArchive, "OpenArchive"},
        {0x0015, &FS_PXI::CommitSaveData, "CommitSaveData"},
        {0x0016, &FS_PXI::CloseArchive, "CloseArchive"},
        {0x0018, &FS_PXI::GetCardType, "GetCardType"},
        {0x0019, &FS_PXI::GetSdmcArchiveResource, "GetSdmcArchiveResource"},
        {0x001A, &FS_PXI::GetNandArchiveResource, "GetNandArchiveResource"},
        {0x001C, &FS_PXI::IsSdmcDetected, "IsSdmcDetected"},
        {0x001D, &FS_PXI::IsSdmcWritable, "IsSdmcWritable"},
        {0x0026, &FS_PXI::CardSlotIsInserted, "CardSlotIsInserted"},
        {0x003B, &FS_PXI::GetLegacyRomHeader, "GetLegacyRomHeader"},
        {0x003C, &FS_PXI::GetLegacyBannerData, "GetLegacyBannerData"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

FS_PXI::~FS_PXI() = default;

u64 FS_PXI::GetClientProgramId(Kernel::HLERequestContext& ctx) const {
    const auto process = ctx.ClientThread()->owner_process.lock();
    return process && process->codeset ? process->codeset->program_id : 0;
}

void FS_PXI::OpenArchive(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto archive_id = rp.PopEnum<ArchiveIdCode>();
    const auto path_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 path_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr path_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> path_data(path_size);
    system.Memory().ReadBlock(*process, path_addr, path_data.data(), path_data.size());
    const FileSys::Path archive_path(path_type, std::move(path_data));

    LOG_DEBUG(Service_FS, "archive_id=0x{:08X} archive_path={}", archive_id,
              archive_path.DebugStr());

    const ResultVal<ArchiveHandle> handle =
        archives.OpenArchive(archive_id, archive_path, GetClientProgramId(ctx));

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(handle.Code());
    if (handle.Succeeded()) {
        rb.PushRaw(*handle);
    } else {
        rb.Push<u64>(0);
        LOG_ERROR(Service_FS, "failed to get a handle for archive archive_id=0x{:08X} path={}",
                  archive_id, archive_path.DebugStr());
    }
}

void FS_PXI::CloseArchive(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.CloseArchive(archive_handle));
}

void FS_PXI::CommitSaveData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const u32 id = rp.Pop<u32>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.ControlArchive(archive_handle, id, nullptr, 0, nullptr, 0));
}

void FS_PXI::OpenFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const auto filename_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 filename_size = rp.Pop<u32>();
    const FileSys::Mode mode{rp.Pop<u32>()};
    const u32 attributes = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr filename_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> filename(filename_size);
    system.Memory().ReadBlock(*process, filename_addr, filename.data(), filename.size());
    const FileSys::Path file_path(filename_type, std::move(filename));

    LOG_DEBUG(Service_FS, "path={}, mode={} attrs={}", file_path.DebugStr(), mode.hex, attributes);

    const auto [file_res, open_timeout_ns] =
        archives.OpenFileFromArchive(archive_handle, file_path, mode, attributes);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(file_res.Code());
    if (file_res.Succeeded()) {
        const u64 handle = next_file_handle++;
        file_handles.emplace(handle, *file_res);
        rb.Push<u64>(handle);
    } else {
        rb.Push<u64>(0);
        LOG_DEBUG(Service_FS, "failed to get a handle for file {}", file_path.DebugStr());
    }

    ctx.SleepClientThread("fs_pxi::open_file", open_timeout_ns, nullptr);
}

void FS_PXI::CloseFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 file_handle = rp.PopRaw<u64>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    auto it = file_handles.find(file_handle);
    if (it == file_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        return;
    }
    it->second->backend->Close();
    file_handles.erase(it);
    rb.Push(ResultSuccess);
}

void FS_PXI::ReadFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 file_handle = rp.PopRaw<u64>();
    const u64 offset = rp.PopRaw<u64>();
    const u32 size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr buffer_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    auto it = file_handles.find(file_handle);
    if (it == file_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        rb.Push(0);
        return;
    }

    std::vector<u8> buffer(size);
    const ResultVal<std::size_t> read =
        it->second->backend->Read(offset, size, buffer.data());
    if (read.Succeeded()) {
        system.Memory().WriteBlock(*process, buffer_addr, buffer.data(), *read);
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(*read));
    } else {
        rb.Push(read.Code());
        rb.Push(0);
    }
}

void FS_PXI::WriteFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 file_handle = rp.PopRaw<u64>();
    const u64 offset = rp.PopRaw<u64>();
    const u32 size = rp.Pop<u32>();
    const u32 flags = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr buffer_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    auto it = file_handles.find(file_handle);
    if (it == file_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        rb.Push(0);
        return;
    }

    std::vector<u8> buffer(size);
    system.Memory().ReadBlock(*process, buffer_addr, buffer.data(), buffer.size());
    const ResultVal<std::size_t> written =
        it->second->backend->Write(offset, size, flags != 0, true, buffer.data());
    if (written.Succeeded()) {
        rb.Push(ResultSuccess);
        rb.Push(static_cast<u32>(*written));
    } else {
        rb.Push(written.Code());
        rb.Push(0);
    }
}

void FS_PXI::GetFileSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 file_handle = rp.PopRaw<u64>();

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    auto it = file_handles.find(file_handle);
    if (it == file_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        rb.Push<u64>(0);
        return;
    }
    rb.Push(ResultSuccess);
    rb.Push<u64>(it->second->backend->GetSize());
}

void FS_PXI::SetFileSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 size = rp.PopRaw<u64>();
    const u64 file_handle = rp.PopRaw<u64>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    auto it = file_handles.find(file_handle);
    if (it == file_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        return;
    }
    it->second->backend->SetSize(size);
    rb.Push(ResultSuccess);
}

void FS_PXI::CreateFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const auto filename_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 filename_size = rp.Pop<u32>();
    const u32 attributes = rp.Pop<u32>();
    const u64 file_size = rp.PopRaw<u64>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr filename_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> filename(filename_size);
    system.Memory().ReadBlock(*process, filename_addr, filename.data(), filename.size());
    const FileSys::Path file_path(filename_type, std::move(filename));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.CreateFileInArchive(archive_handle, file_path, file_size, attributes));
}

void FS_PXI::DeleteFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const auto filename_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 filename_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr filename_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> filename(filename_size);
    system.Memory().ReadBlock(*process, filename_addr, filename.data(), filename.size());
    const FileSys::Path file_path(filename_type, std::move(filename));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.DeleteFileFromArchive(archive_handle, file_path));
}

void FS_PXI::RenameFile(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle src_archive_handle = rp.PopRaw<u64>();
    const auto src_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 src_size = rp.Pop<u32>();
    const ArchiveHandle dst_archive_handle = rp.PopRaw<u64>();
    const auto dst_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 dst_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr src_addr = rp.Pop<VAddr>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr dst_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> src_path_data(src_size);
    std::vector<u8> dst_path_data(dst_size);
    system.Memory().ReadBlock(*process, src_addr, src_path_data.data(), src_path_data.size());
    system.Memory().ReadBlock(*process, dst_addr, dst_path_data.data(), dst_path_data.size());
    const FileSys::Path src_path(src_type, std::move(src_path_data));
    const FileSys::Path dst_path(dst_type, std::move(dst_path_data));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.RenameFileBetweenArchives(src_archive_handle, src_path,
                                               dst_archive_handle, dst_path));
}

void FS_PXI::OpenDirectory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const auto dir_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 dir_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr dir_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> dir_path_data(dir_size);
    system.Memory().ReadBlock(*process, dir_addr, dir_path_data.data(), dir_path_data.size());
    const FileSys::Path dir_path(dir_type, std::move(dir_path_data));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    const ResultVal<std::shared_ptr<Directory>> dir_res =
        archives.OpenDirectoryFromArchive(archive_handle, dir_path);
    rb.Push(dir_res.Code());
    if (dir_res.Succeeded()) {
        const u64 handle = next_dir_handle++;
        dir_handles.emplace(handle, *dir_res);
        rb.Push<u64>(handle);
    } else {
        rb.Push<u64>(0);
        LOG_DEBUG(Service_FS, "failed to get a handle for directory path={}", dir_path.DebugStr());
    }
}

void FS_PXI::CloseDirectory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 dir_handle = rp.PopRaw<u64>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    auto it = dir_handles.find(dir_handle);
    if (it == dir_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        return;
    }
    it->second->backend->Close();
    dir_handles.erase(it);
    rb.Push(ResultSuccess);
}

void FS_PXI::ReadDirectory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 dir_handle = rp.PopRaw<u64>();
    const u32 entry_count = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr entries_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    auto it = dir_handles.find(dir_handle);
    if (it == dir_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        rb.Push(0);
        return;
    }

    std::vector<FileSys::Entry> entries(entry_count);
    const u32 read = it->second->backend->Read(entry_count, entries.data());
    system.Memory().WriteBlock(*process, entries_addr, entries.data(),
                               static_cast<std::size_t>(read) * sizeof(FileSys::Entry));
    rb.Push(ResultSuccess);
    rb.Push(read);
}

void FS_PXI::CreateDirectory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const auto dir_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 dir_size = rp.Pop<u32>();
    const u32 attributes = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr dir_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> dir_path_data(dir_size);
    system.Memory().ReadBlock(*process, dir_addr, dir_path_data.data(), dir_path_data.size());
    const FileSys::Path dir_path(dir_type, std::move(dir_path_data));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.CreateDirectoryFromArchive(archive_handle, dir_path, attributes));
}

void FS_PXI::DeleteDirectory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle archive_handle = rp.PopRaw<u64>();
    const auto dir_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 dir_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr dir_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> dir_path_data(dir_size);
    system.Memory().ReadBlock(*process, dir_addr, dir_path_data.data(), dir_path_data.size());
    const FileSys::Path dir_path(dir_type, std::move(dir_path_data));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.DeleteDirectoryFromArchive(archive_handle, dir_path));
}

void FS_PXI::RenameDirectory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false); // Transaction
    const ArchiveHandle src_archive_handle = rp.PopRaw<u64>();
    const auto src_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 src_size = rp.Pop<u32>();
    const ArchiveHandle dst_archive_handle = rp.PopRaw<u64>();
    const auto dst_type = rp.PopEnum<FileSys::LowPathType>();
    const u32 dst_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr src_addr = rp.Pop<VAddr>();
    rp.Pop<u32>(); // PXI buffer descriptor
    const VAddr dst_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();
    std::vector<u8> src_path_data(src_size);
    std::vector<u8> dst_path_data(dst_size);
    system.Memory().ReadBlock(*process, src_addr, src_path_data.data(), src_path_data.size());
    system.Memory().ReadBlock(*process, dst_addr, dst_path_data.data(), dst_path_data.size());
    const FileSys::Path src_path(src_type, std::move(src_path_data));
    const FileSys::Path dst_path(dst_type, std::move(dst_path_data));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(archives.RenameDirectoryBetweenArchives(src_archive_handle, src_path,
                                                    dst_archive_handle, dst_path));
}

void FS_PXI::CalcSavegameMAC(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u64 file_handle = rp.PopRaw<u64>();
    const u32 out_size = rp.Pop<u32>();
    const u32 in_size = rp.Pop<u32>();
    rp.Pop<u32>(); // PXI buffer descriptor (input)
    const VAddr in_addr = rp.Pop<VAddr>();
    rp.Pop<u32>(); // PXI buffer descriptor (output)
    const VAddr out_addr = rp.Pop<VAddr>();

    auto process = ctx.ClientThread()->owner_process.lock();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (file_handles.find(file_handle) == file_handles.end()) {
        rb.Push(FileSys::ResultInvalidArchiveHandle);
        return;
    }

    std::vector<u8> input(in_size);
    system.Memory().ReadBlock(*process, in_addr, input.data(), input.size());

    // See https://3dbrew.org/wiki/FSPXI:CalcSavegameMAC and PKSM's loader.cpp. The chain is
    //   sav0 = SHA256("CTR-SAV0" || input)
    //   sign = SHA256("CTR-SIGN" || title_id || sav0)
    //   MAC  = AES-CMAC(sign, hardware_key)
    // The hardware key cannot be reproduced in HLE, and the title ID is not plumbed through the
    // file handle, so use a fixed zero key and omit the title ID. The result is deterministic and
    // self-consistent for saves written and read back through this service (PKSM's workflow).
    static constexpr std::array<u8, CryptoPP::AES::DEFAULT_KEYLENGTH> zero_key{};
    static constexpr char CTR_SAV0[] = "CTR-SAV0";
    static constexpr char CTR_SIGN[] = "CTR-SIGN";

    std::array<u8, CryptoPP::SHA256::DIGESTSIZE> sav0{};
    {
        CryptoPP::SHA256 sha;
        sha.Update(reinterpret_cast<const u8*>(CTR_SAV0), sizeof(CTR_SAV0) - 1);
        sha.Update(input.data(), input.size());
        sha.Final(sav0.data());
    }

    std::array<u8, CryptoPP::SHA256::DIGESTSIZE> sign{};
    {
        CryptoPP::SHA256 sha;
        sha.Update(reinterpret_cast<const u8*>(CTR_SIGN), sizeof(CTR_SIGN) - 1);
        sha.Update(sav0.data(), sav0.size());
        sha.Final(sign.data());
    }

    std::vector<u8> output(out_size, 0);
    CryptoPP::CMAC<CryptoPP::AES> cmac;
    cmac.SetKey(zero_key.data(), zero_key.size());
    cmac.Update(sign.data(), sign.size());
    const u32 mac_size = std::min<u32>(out_size, static_cast<u32>(cmac.DigestSize()));
    cmac.TruncatedFinal(output.data(), mac_size);

    system.Memory().WriteBlock(*process, out_addr, output.data(), mac_size);
    rb.Push(ResultSuccess);
}

void FS_PXI::GetCardType(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);

    const auto& cartridge = system.GetCartridge();
    u8 card_type = 0; // CTR Card (3DS)
    if (!cartridge.empty() && FileSys::IsNDSROM(cartridge)) {
        card_type = 1; // TWL Card (DS/DSi)
    }
    rb.Push(card_type);
}

void FS_PXI::GetSdmcArchiveResource(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(5, 0);
    const ResultVal<ArchiveResource> resource = archives.GetArchiveResource(MediaType::SDMC);
    if (resource.Failed()) {
        rb.Push(resource.Code());
        rb.Skip(4, false);
        return;
    }
    rb.Push(ResultSuccess);
    rb.Push(resource->sector_size_in_bytes);
    rb.Push(resource->cluster_size_in_bytes);
    rb.Push(resource->partition_capacity_in_clusters);
    rb.Push(resource->free_space_in_clusters);
}

void FS_PXI::GetNandArchiveResource(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(5, 0);
    const ResultVal<ArchiveResource> resource = archives.GetArchiveResource(MediaType::NAND);
    if (resource.Failed()) {
        rb.Push(resource.Code());
        rb.Skip(4, false);
        return;
    }
    rb.Push(ResultSuccess);
    rb.Push(resource->sector_size_in_bytes);
    rb.Push(resource->cluster_size_in_bytes);
    rb.Push(resource->partition_capacity_in_clusters);
    rb.Push(resource->free_space_in_clusters);
}

void FS_PXI::IsSdmcDetected(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(Settings::values.use_virtual_sd.GetValue());
}

void FS_PXI::IsSdmcWritable(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(Settings::values.use_virtual_sd.GetValue());
}

void FS_PXI::CardSlotIsInserted(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(!system.GetCartridge().empty());
}

void FS_PXI::GetLegacyRomHeader(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto media_type = rp.PopEnum<MediaType>();
    const auto title_id = rp.Pop<u64>();
    rp.Pop<u32>(); // reserved
    auto output = rp.PopMappedBuffer();

    constexpr u32 NDS_HEADER_SIZE = 0x3B4;
    std::vector<u8> header(NDS_HEADER_SIZE, 0);

    bool success = false;
    if (media_type == MediaType::GameCard) {
        const auto& cartridge = system.GetCartridge();
        if (!cartridge.empty() && FileSys::IsNDSROM(cartridge)) {
            FileUtil::IOFile file(cartridge, "rb");
            if (file.IsOpen()) {
                file.Seek(0, SEEK_SET);
                success = file.ReadBytes(header.data(), NDS_HEADER_SIZE) == NDS_HEADER_SIZE;
            }
        }
    }

    if (success) {
        output.Write(header.data(), 0, header.size());
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(success ? ResultSuccess : FileSys::ResultNotFound);

    LOG_DEBUG(Service_FS, "GetLegacyRomHeader(PXI): media_type={}, title_id={:016X}, success={}",
              static_cast<u8>(media_type), title_id, success);
}

void FS_PXI::GetLegacyBannerData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto media_type = rp.PopEnum<MediaType>();
    const auto title_id = rp.Pop<u64>();
    rp.Pop<u32>(); // reserved
    auto output = rp.PopMappedBuffer();

    constexpr u32 BANNER_SIZE = 0x23C0;
    std::vector<u8> banner(BANNER_SIZE, 0);

    bool success = false;
    if (media_type == MediaType::GameCard) {
        const auto& cartridge = system.GetCartridge();
        if (!cartridge.empty() && FileSys::IsNDSROM(cartridge)) {
            FileUtil::IOFile file(cartridge, "rb");
            if (file.IsOpen()) {
                // Read NDS header to get banner offset
                FileSys::NDSROMHeader nds_header{};
                if (file.ReadBytes(&nds_header, sizeof(nds_header)) == sizeof(nds_header)) {
                    const u32 banner_offset = nds_header.banner_offset;
                    const u64 file_size = file.GetSize();
                    if (banner_offset > 0 &&
                        static_cast<u64>(banner_offset) + BANNER_SIZE <= file_size) {
                        file.Seek(banner_offset, SEEK_SET);
                        success = file.ReadBytes(banner.data(), BANNER_SIZE) == BANNER_SIZE;
                    }
                }
            }
        }
    }

    if (success) {
        output.Write(banner.data(), 0, banner.size());
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(success ? ResultSuccess : FileSys::ResultNotFound);

    LOG_DEBUG(Service_FS, "GetLegacyBannerData(PXI): media_type={}, title_id={:016X}, success={}",
              static_cast<u8>(media_type), title_id, success);
}

template <class Archive>
void Service::FS::FS_PXI::serialize(Archive& ar, const unsigned int) {
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    // Open handles are ephemeral and dropped on savestate; the guest simply gets invalid handle
    // errors if it keeps using them, like other services that lose session state.
    ar & next_file_handle;
    ar & next_dir_handle;
    if (Archive::is_loading::value) {
        file_handles.clear();
        dir_handles.clear();
    }
}
SERIALIZE_IMPL(Service::FS::FS_PXI)

} // namespace Service::FS
