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

    // Set region lockout (region-free for TWL games)
    smdh.region_lockout = 0x7FFFFFFF;

    // Set flags (visible on Home Menu, allow 3D)
    smdh.flags = 0x0001 | 0x0004; // Visible + Allow 3D

    // Generate a simple icon (24x24 and 48x48 RGB565)
    // Blue background with white "DS" text
    for (u32 y = 0; y < 48; y++) {
        for (u32 x = 0; x < 48; x++) {
            u32 idx = (y * 48 + x) * 2;
            // Blue background (0x001F = 0,0,31 in RGB565)
            u16 pixel = 0x001F;

            // Draw "D" shape (simplified)
            if (y >= 8 && y < 40 && x >= 12 && x < 18) {
                pixel = 0xFFFF; // White
            }
            if (y >= 8 && y < 14 && x >= 12 && x < 28) {
                pixel = 0xFFFF; // White
            }
            if (y >= 34 && y < 40 && x >= 12 && x < 28) {
                pixel = 0xFFFF; // White
            }
            if (y >= 8 && y < 40 && x >= 22 && x < 28) {
                if (y < 14 || y >= 34) {
                    pixel = 0xFFFF; // White
                } else if (y >= 18 && y < 30) {
                    pixel = 0xFFFF; // White
                }
            }

            // Draw "S" shape (simplified)
            if (y >= 8 && y < 14 && x >= 30 && x < 42) {
                pixel = 0xFFFF; // White
            }
            if (y >= 8 && y < 24 && x >= 30 && x < 36) {
                pixel = 0xFFFF; // White
            }
            if (y >= 18 && y < 24 && x >= 30 && x < 42) {
                pixel = 0xFFFF; // White
            }
            if (y >= 18 && y < 40 && x >= 36 && x < 42) {
                pixel = 0xFFFF; // White
            }
            if (y >= 34 && y < 40 && x >= 30 && x < 42) {
                pixel = 0xFFFF; // White
            }

            // Large icon (48x48)
            if (idx < sizeof(smdh.large_icon)) {
                smdh.large_icon[idx] = pixel & 0xFF;
                smdh.large_icon[idx + 1] = (pixel >> 8) & 0xFF;
            }

            // Small icon (24x24) - scaled down version
            if (x < 24 && y < 24) {
                u32 small_idx = (y * 24 + x) * 2;
                if (small_idx < sizeof(smdh.small_icon)) {
                    smdh.small_icon[small_idx] = pixel & 0xFF;
                    smdh.small_icon[small_idx + 1] = (pixel >> 8) & 0xFF;
                }
            }
        }
    }

    // Set titles from NDS header
    char game_title[13] = {};
    std::memcpy(game_title, nds_header.game_title, 12);

    // Set titles for all languages
    for (u32 lang = 0; lang < 16; lang++) {
        auto& title_entry = smdh.titles[lang];

        // Convert ASCII to UTF-16 for short title
        for (u32 i = 0; i < 0x40 && game_title[i] != '\0'; i++) {
            title_entry.short_title[i] = static_cast<char16_t>(game_title[i]);
        }

        // Set long title (same as short title for TWL games)
        for (u32 i = 0; i < 0x80 && game_title[i] != '\0'; i++) {
            title_entry.long_title[i] = static_cast<char16_t>(game_title[i]);
        }

        // Set publisher (unknown for TWL games)
        const char* publisher = "NDS Game";
        for (u32 i = 0; i < 0x40 && publisher[i] != '\0'; i++) {
            title_entry.publisher[i] = static_cast<char16_t>(publisher[i]);
        }
    }

    LOG_INFO(Loader, "Generated synthetic SMDH for NDS ROM: '{}'", game_title);

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
