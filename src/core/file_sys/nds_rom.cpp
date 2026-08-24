// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/file_sys/nds_rom.h"

namespace FileSys {

bool IsNDSROM(const std::string& filepath) {
    FileUtil::IOFile file(filepath, "rb");
    if (!file.IsOpen()) {
        return false;
    }

    if (file.GetSize() < sizeof(NDSROMHeader)) {
        return false;
    }

    // Read the first few bytes to check for NDS ROM signature
    // NDS ROMs have valid ARM9/ARM7 entry points in the header
    NDSROMHeader header{};
    if (file.ReadBytes(&header, sizeof(header)) != sizeof(header)) {
        return false;
    }

    // Check for valid ARM9 ROM offset (must be >= 0x200)
    if (header.arm9_rom_offset < 0x200) {
        return false;
    }

    // Check for valid ARM9 size
    if (header.arm9_size == 0) {
        return false;
    }

    // Check for valid game code (not all zeros or 0xFF)
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 4; i++) {
        if (header.game_code[i] != 0) all_zero = false;
        if (header.game_code[i] != 0xFF) all_ff = false;
    }
    if (all_zero || all_ff) {
        return false;
    }

    return true;
}

bool ReadNDSROMHeader(const std::string& filepath, NDSROMHeader& header) {
    FileUtil::IOFile file(filepath, "rb");
    if (!file.IsOpen()) {
        LOG_ERROR(Loader, "Failed to open NDS ROM: {}", filepath);
        return false;
    }

    if (file.GetSize() < sizeof(NDSROMHeader)) {
        LOG_ERROR(Loader, "NDS ROM too small: {} bytes", file.GetSize());
        return false;
    }

    std::memset(&header, 0, sizeof(header));
    if (file.ReadBytes(&header, sizeof(header)) != sizeof(header)) {
        LOG_ERROR(Loader, "Failed to read NDS ROM header from {}", filepath);
        return false;
    }

    // Validate the header
    if (!IsNDSROM(filepath)) {
        LOG_ERROR(Loader, "Invalid NDS ROM header in {}", filepath);
        return false;
    }

    LOG_INFO(Loader, "Read NDS ROM header: title='{}', code='{}', maker='{}', DSi={}",
             GetNDSGameTitle(header), std::string(header.game_code, 4),
             GetNDSMakerCode(header), IsNDSiROM(header));

    return true;
}

} // namespace FileSys
