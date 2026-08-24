// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_types.h"
#include "core/file_sys/nds_rom.h"

namespace FileSys {

/// SMDH total size (0x36C0 bytes)
constexpr u32 SMDH_TOTAL_SIZE = 0x36C0;

/// SMDH title entry (per language) - 0x200 bytes each
struct SMDHTitleEntry {
    char16_t short_title[0x40];    // 64 chars (0x80 bytes)
    char16_t long_title[0x80];     // 128 chars (0x100 bytes)
    char16_t publisher[0x40];      // 64 chars (0x80 bytes)
};
static_assert(sizeof(SMDHTitleEntry) == 0x200, "SMDHTitleEntry must be 0x200 bytes");

/// SMDH structure (compatible with 3DS SMDH format)
/// Total size: 0x36C0 bytes (14016)
struct SMDHData {
    u32 magic;                          // "SMDH" (0x48444D53)
    u16 version;                        // 0x0001
    u16 reserved1;
    SMDHTitleEntry titles[16];          // 16 language titles (0x2000 bytes)
    u8 age_ratings[16];                // Age rating flags
    u32 region_lockout;                 // Region lockout bitmask
    u8 match_maker_id[4];              // Match Maker ID
    u8 match_maker_bit_id[8];          // Match Maker BIT ID
    u32 flags;                          // Application flags
    u16 eula_version;                   // EULA version
    u16 reserved2;
    float optimal_animation_frame;      // Optimal animation frame
    u32 cec_id;                         // CEC (StreetPass) ID
    u8 reserved3[8];
    // Icon graphics at offset 0x2040
    u8 small_icon[24 * 24 * 2];        // 24x24 RGB565 (0x480 bytes)
    u8 large_icon[48 * 48 * 2];        // 48x48 RGB565 (0x1200 bytes)
};
static_assert(sizeof(SMDHData) == 0x36C0, "SMDHData must be 0x36C0 bytes");

/// Generate synthetic SMDH data from NDS ROM header and banner
/// Returns empty vector on failure
std::vector<u8> GenerateNDS_SMDH(const NDSROMHeader& nds_header);

/// Check if a file contains valid SMDH data
bool IsSMDHData(const std::vector<u8>& data);

} // namespace FileSys
