// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstring>
#include <string>
#include "common/common_types.h"
#include "common/file_util.h"

namespace FileSys {

/// NDS ROM header structure (4096 bytes)
/// Based on melonDS NDS_Header.h and GBATEK documentation
#pragma pack(push, 1)
struct NDSROMHeader {
    char game_title[12];
    char game_code[4];
    char maker_code[2];
    u8 unit_code;           // 0x00=NDS, 0x02=NDSi
    u8 encryption_seed_select;
    u8 card_size;
    u8 reserved1[7];
    u8 dsi_crypto_flags;
    u8 nds_region;
    u8 rom_version;
    u8 autostart;

    u32 arm9_rom_offset;
    u32 arm9_entry_address;
    u32 arm9_ram_address;
    u32 arm9_size;

    u32 arm7_rom_offset;
    u32 arm7_entry_address;
    u32 arm7_ram_address;
    u32 arm7_size;

    u32 fnt_offset;
    u32 fnt_size;
    u32 fat_offset;
    u32 fat_size;

    u32 arm9_overlay_offset;
    u32 arm9_overlay_size;
    u32 arm7_overlay_offset;
    u32 arm7_overlay_size;

    u32 normal_command_settings;
    u32 key1_command_settings;

    u32 banner_offset;

    u16 secure_area_crc16;
    u16 secure_area_delay;

    u32 arm9_autoload_list_address;
    u32 arm7_autoload_list_address;

    u64 secure_area_disable;

    u32 rom_size;           // excluding DSi area
    u32 header_size;

    u32 dsi_arm9_param_table_offset;
    u32 dsi_arm7_param_table_offset;

    u16 nds_region_end;
    u16 dsi_region_start;

    u16 nand_rom_end;
    u16 nand_rw_start;

    u8 reserved2[40];

    u8 nintendo_logo[156];
    u16 nintendo_logo_crc16;
    u16 header_crc16;

    u32 debug_rom_offset;
    u32 debug_size;
    u32 debug_ram_address;

    u32 reserved4;
    u8 reserved5[16];

    u32 dsi_mbk_slots[5];
    u32 dsi_arm9_mbk_areas[3];
    u32 dsi_arm7_mbk_areas[3];
    u8 dsi_mbk_write_protect[3];
    u8 dsi_wramcnt_setting;

    u32 dsi_region_mask;
    u32 dsi_permissions[2];
    u8 reserved6[3];
    u8 app_flags;

    u32 dsi_arm9i_rom_offset;
    u32 reserved7;
    u32 dsi_arm9i_ram_address;
    u32 dsi_arm9i_size;

    u32 dsi_arm7i_rom_offset;
    u32 dsi_sdmmc_device_list;
    u32 dsi_arm7i_ram_address;
    u32 dsi_arm7i_size;

    u32 dsi_digest_ntr_offset;
    u32 dsi_digest_ntr_size;
    u32 dsi_digest_twl_offset;
    u32 dsi_digest_twl_size;
    u32 dsi_digest_sec_hashtbl_offset;
    u32 dsi_digest_sec_hashtbl_size;
    u32 dsi_digest_blk_hashtbl_offset;
    u32 dsi_digest_blk_hashtbl_size;
    u32 dsi_digest_sec_size;
    u32 dsi_digest_blk_sec_count;

    u32 dsi_banner_size;

    u8 dsi_shared0_size;
    u8 dsi_shared1_size;
    u8 dsi_ela_ratings;
    u8 dsi_use_ratings;
    u32 dsi_total_rom_size;
    u8 dsi_shared2_size;
    u8 dsi_shared3_size;
    u8 dsi_shared4_size;
    u8 dsi_shared5_size;

    u32 dsi_arm9iParam_table_offset;
    u32 dsi_arm7iParam_table_offset;

    u32 dsi_modcrypt1_offset;
    u32 dsi_modcrypt1_size;
    u32 dsi_modcrypt2_offset;
    u32 dsi_modcrypt2_size;

    u32 dsi_title_id_low;
    u32 dsi_title_id_high;

    u32 dsi_public_sav_size;
    u32 dsi_private_sav_size;

    u8 reserved8[176];

    u8 dsi_age_rating_flags[16];

    u8 dsi_arm9_hash[20];
    u8 dsi_arm7_hash[20];
    u8 dsi_digest_master_hash[20];
    u8 banner_hash[20];
    u8 dsi_arm9i_hash[20];
    u8 dsi_arm7i_hash[20];
    u8 header_binaries_hash[20];
    u8 arm9_overlay_hash[20];
    u8 dsi_arm9_no_secure_hash[20];

    u8 reserved9[2636];

    u8 reserved10[384];

    u8 header_signature[128];
};
#pragma pack(pop)

static_assert(sizeof(NDSROMHeader) == 4096, "NDSROMHeader must be 4096 bytes");

/// Check if a file is a valid NDS ROM by examining the header
bool IsNDSROM(const std::string& filepath);

/// Read the NDS ROM header from a file
/// Returns true on success
bool ReadNDSROMHeader(const std::string& filepath, NDSROMHeader& header);

/// Get the game code as a u32 (little-endian)
inline u32 GetNDSGameCode(const NDSROMHeader& header) {
    return static_cast<u32>(header.game_code[0]) |
           (static_cast<u32>(header.game_code[1]) << 8) |
           (static_cast<u32>(header.game_code[2]) << 16) |
           (static_cast<u32>(header.game_code[3]) << 24);
}

/// Get the maker code as a string
inline std::string GetNDSMakerCode(const NDSROMHeader& header) {
    return std::string(header.maker_code, 2);
}

/// Get the game title as a string (null-terminated)
inline std::string GetNDSGameTitle(const NDSROMHeader& header) {
    char title[13] = {};
    std::memcpy(title, header.game_title, 12);
    return std::string(title);
}

/// Check if this is a DSi-enhanced ROM
inline bool IsNDSiROM(const NDSROMHeader& header) {
    return (header.unit_code & 0x02) != 0;
}

/// Generate a synthetic 3DS title ID from NDS game code
/// Format: high=0x00030005 (TWL application), low=game_code<<8
/// This matches how the 3DS represents TWL application titles on game cards
inline u64 GetNDSSyntheticTitleID(const NDSROMHeader& header) {
    const u32 game_code = GetNDSGameCode(header);
    return (static_cast<u64>(0x00030005) << 32) | (static_cast<u64>(game_code) << 8);
}

} // namespace FileSys
