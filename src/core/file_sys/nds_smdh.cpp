// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include "common/logging/log.h"
#include "core/file_sys/nds_smdh.h"

namespace FileSys {

std::vector<u8> GenerateNDS_SMDH(const NDSROMHeader& nds_header) {
    SMDHData smdh{};
    smdh.magic = 0x48444D53; // "SMDH" in little-endian
    smdh.version = 0x0001;

    // Set age rating (ESRB Everyone for all regions)
    smdh.age_ratings[0] = 0x00; // JPN
    smdh.age_ratings[1] = 0x00; // USA
    smdh.age_ratings[2] = 0x00; // EUR
    smdh.age_ratings[3] = 0x00; // AUS
    smdh.age_ratings[4] = 0x00; // CHN
    smdh.age_ratings[5] = 0x00; // KOR
    smdh.age_ratings[6] = 0x00; // TWN

    // Generate a simple icon (32x32 RGB565)
    // Blue background with white "DS" text
    for (u32 y = 0; y < 32; y++) {
        for (u32 x = 0; x < 32; x++) {
            u32 idx = (y * 32 + x) * 2;
            // Blue background (0x001F = 0,0,31 in RGB565)
            u16 pixel = 0x001F;

            // Draw "D" shape (simplified)
            if (y >= 4 && y < 28 && x >= 8 && x < 12) {
                pixel = 0xFFFF; // White
            }
            if (y >= 4 && y < 8 && x >= 8 && x < 20) {
                pixel = 0xFFFF; // White
            }
            if (y >= 24 && y < 28 && x >= 8 && x < 20) {
                pixel = 0xFFFF; // White
            }
            if (y >= 4 && y < 28 && x >= 16 && x < 20) {
                if (y < 8 || y >= 24) {
                    pixel = 0xFFFF; // White
                } else if (y >= 12 && y < 20) {
                    pixel = 0xFFFF; // White
                }
            }

            // Draw "S" shape (simplified)
            if (y >= 4 && y < 8 && x >= 22 && x < 30) {
                pixel = 0xFFFF; // White
            }
            if (y >= 4 && y < 16 && x >= 22 && x < 26) {
                pixel = 0xFFFF; // White
            }
            if (y >= 12 && y < 16 && x >= 22 && x < 30) {
                pixel = 0xFFFF; // White
            }
            if (y >= 12 && y < 28 && x >= 26 && x < 30) {
                pixel = 0xFFFF; // White
            }
            if (y >= 24 && y < 28 && x >= 22 && x < 30) {
                pixel = 0xFFFF; // White
            }

            smdh.icon_data[idx] = pixel & 0xFF;
            smdh.icon_data[idx + 1] = (pixel >> 8) & 0xFF;
        }
    }

    // Set titles from NDS header
    char game_title[13] = {};
    std::memcpy(game_title, nds_header.game_title, 12);

    // Convert ASCII to UTF-16 for each language
    for (u32 lang = 0; lang < 16; lang++) {
        for (u32 i = 0; i < 0x100 && game_title[i] != '\0'; i++) {
            smdh.titles[lang][i] = static_cast<char16_t>(game_title[i]);
        }
    }

    // Add region info to title
    const char* region_str = "";
    switch (nds_header.nds_region) {
    case 0x00: region_str = " (JPN)"; break;
    case 0x40: region_str = " (USA)"; break;
    case 0x80: region_str = " (EUR)"; break;
    case 0xC0: region_str = " (AUS)"; break;
    default: break;
    }

    if (region_str[0] != '\0') {
        for (u32 lang = 0; lang < 16; lang++) {
            u32 len = 0;
            while (smdh.titles[lang][len] != 0 && len < 0xFE) {
                len++;
            }
            for (u32 i = 0; region_str[i] != '\0' && (len + i) < 0xFF; i++) {
                smdh.titles[lang][len + i] = static_cast<char16_t>(region_str[i]);
            }
        }
    }

    LOG_INFO(FileSys, "Generated synthetic SMDH for NDS ROM: '{}'", game_title);

    std::vector<u8> result(sizeof(SMDHData));
    std::memcpy(result.data(), &smdh, sizeof(smdh));
    return result;
}

bool IsSMDHData(const std::vector<u8>& data) {
    if (data.size() < 4) {
        return false;
    }

    // Check for "SMDH" magic
    const u32 magic = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    return magic == 0x48444D53; // "SMDH"
}

} // namespace FileSys
