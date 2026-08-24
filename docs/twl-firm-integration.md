# TWL_FIRM Integration Roadmap

## Overview

TWL_FIRM is the DS/DSi firmware that runs on the3DS when playing NTR (DS/DSi) games. This document describes the integration plan for Azahar to support DS/DSi game playback.

## Current Status

### Phase 1: Game Card Detection (Implemented)
- ✅ NDS ROM header parser (`src/core/file_sys/nds_rom.h/cpp`)
- ✅ `FSUSER:GetCardType` returns TWL card type (1) for .nds files
- ✅ `AM_GetTitleList` detects TWL ROMs and generates synthetic title IDs
- ✅ Home Menu shows inserted .nds files as game cards

### Phase 2: Synthetic SMDH Data (Next)
- [ ] Create synthetic SMDH data for TWL game cards
- [ ] Parse NDS banner data for icon/title information
- [ ] Generate SMDH-compatible icon data from NDS banner

### Phase 3: melonDS Integration (Future)
- [ ] Vendor melonDS as a subsystem library
- [ ] Implement TWL_FIRM boot sequence
- [ ] Handle ARM7/ARM9 dual-core emulation
- [ ] Implement NDS cartridge emulation
- [ ] Handle TWL-specific hardware (camera, mic, wifi)

### Phase 4: Full TWL Support (Long-term)
- [ ] DSiWare title installation
- [ ] TWL NAND partition support
- [ ] TWL-specific save data handling
- [ ] Local wireless (TWL multi-cart)

## Technical Details

### NDS ROM Header Structure
The NDS ROM header is 4096 bytes and contains:
- `game_title[12]` - Game title (null-terminated)
- `game_code[4]` - Game code (unique identifier)
- `maker_code[2]` - Publisher code
- `unit_code` - 0x00=NDS, 0x02=NDSi
- `arm9_rom_offset` - ARM9 binary offset
- `arm9_size` - ARM9 binary size
- `banner_offset` - Banner data offset (contains icon/title)

### Synthetic Title ID Generation
For TWL titles, the 3DS uses a synthetic title ID format:
- `0x00030000 | (game_code << 8)`
- Example: Game code "CPUE" → Title ID `0x0003000045555000`

### Home Menu Game Card Detection Flow
1. `FSUSER:CardSlotIsInserted` → returns true if cartridge path is set
2. `FSUSER:GetCardType` → returns 0 (CTR) or 1 (TWL)
3. `AM_GetTitleList(GameCard)` → returns title IDs on the card
4. Home Menu reads card's ExeFS:/icon for SMDH data
5. For TWL cards, we need to provide synthetic SMDH data

### melonDS Integration Architecture
```
┌─────────────────────────────────────┐
│           Azahar Core               │
├─────────────────────────────────────┤
│  TWL_FIRM Manager                   │
│  - Boot sequence management         │
│  - Memory mapping                   │
│  - Hardware register emulation      │
├─────────────────────────────────────┤
│  melonDS Subsystem                  │
│  - NDS CPU (ARM7/ARM9) emulation    │
│  - NDS GPU (2D/3D) emulation        │
│  - NDS cartridge emulation          │
│  - NDS audio (PSG/Mixer)            │
├─────────────────────────────────────┤
│  Hardware Abstraction               │
│  - Camera (shared with 3DS)         │
│  - Microphone (shared with 3DS)     │
│  - WiFi (separate NDS stack)        │
└─────────────────────────────────────┘
```

## BIOS Files Required

### NDS BIOS (from ref/ds_bios/nds/)
- `biosnds7.rom` - ARM7 BIOS (16KB)
- `biosnds9.bin` - ARM9 BIOS (4KB)

### DSi BIOS (from ref/ds_bios/dsi/)
- `BIOSDSI7.ROM` - DSi ARM7 BIOS
- `bios7i.bin` - DSi ARM7 BIOS (variant)
- `bios9i.bin` - DSi ARM9 BIOS

### DSi NAND (from ref/ds_bios/dsi/NAND/)
- Required for DSi mode emulation
- Contains DSi system files

## Key Challenges

1. **Memory Mapping**: NDS uses different memory layout than 3DS
2. **Dual-Core Emulation**: ARM7 + ARM9 running simultaneously
3. **Hardware Timing**: NDS has different clock speeds and timing
4. **Save Data**: Different save formats (EEPROM, Flash, etc.)
5. **Wi-Fi**: NDS has separate wireless stack

## References

- [3DBrew: TWL_FIRM](https://www.3dbrew.org/wiki/FIRM#TWL_FIRM)
- [DSiBrew: Bootloader](http://dsibrew.org/wiki/Bootloader)
- [melonDS Source](https://github.com/melonDS-emu/melonDS)
- [GBATEK: NDS Cartridge](https://problemkaputt.de/gbatek.htm#dscartridgeioports)
