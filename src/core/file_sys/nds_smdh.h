// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"
#include "core/file_sys/nds_rom.h"

namespace FileSys {

/// SMDH icon size (32x32 pixels, RGB565)
constexpr u32 SMDH_ICON_SIZE = 32 * 32 * 2; // 2048 bytes

/// SMDH title entry (per language)
struct SMDHTitleEntry {
    char16_t short_title[0x40];    // 64 chars
    char16_t long_title[0x80];     // 128 chars
    char16_t publisher[0x40];      // 64 chars
};
static_assert(sizeof(SMDHTitleEntry) == 0x140, "SMDHTitleEntry must be 0x140 bytes");

/// SMDH structure (compatible with 3DS SMDH format)
struct SMDHData {
    u32 magic;                      // "SMDH"
    u16 version;                    // 0x0001
    u16 reserved1;
    u8 age_ratings[16];            // Age rating flags
    u8 icon_data[SMDH_ICON_SIZE]; // 32x32 RGB565 icon
    u16 reserved2[3];
    char16_t titles[16][0x100];    // Titles in 16 languages
};
static_assert(sizeof(SMDHData) == 0x2020, "SMDHData must be 0x2020 bytes");

/// Generate synthetic SMDH data from NDS ROM header and banner
/// Returns empty vector on failure
std::vector<u8> GenerateNDS_SMDH(const NDSROMHeader& nds_header);

/// Check if a file contains valid SMDH data
bool IsSMDHData(const std::vector<u8>& data);

} // namespace FileSys
