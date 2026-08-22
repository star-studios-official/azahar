# 3DS game card emulation (making the Home Menu see an inserted card)

Sources: `ref/3dbrew/wiki/Gamecards.md`, `ref/3dbrew/wiki/Home Menu.md`, `ref/3dbrew/wiki/Filesystem services.md`, `ref/ctr/include/nn/gd/`, `ref/ctr/sources/libraries/gd/`, `src/core/hle/service/fs/`.

## How the real hardware works

The game card ("CCI" cartridge) is a 3DS-specific flash ROM on a shared 8-bit data bus with the NAND and the SD. Two things talk to it:

- **ARM9 / Process9 side** — the low-level card interface (`gd` = "gamecard device", `cardspi` for the save SPI flash). The SDK's `include/nn/gd/` and `sources/libraries/gd/CTR/` are the official PICA200 `gd` library, and `Gamecards.md` documents the command protocol (8-byte DS-compatible commands, then 16-byte encrypted 3DS commands after command `0x3e`, CRC32 per 0x200-byte block, etc.).
- **ARM11 / FS service side** — what the Home Menu actually sees. This is the layer an HLE emulator must reproduce.

## What the Home Menu checks (the emulation surface)

From `ref/3dbrew/wiki/Home Menu.md`:

- **`FSUSER:CardSlotIsInserted`** — is a card physically present? (Real HW: `CFG9_CARDSTATUS` bit 0; PXI; the FS service answers.)
- **`FSUSER:GetCardType`** — which card type is inserted (CTR, TWL, ...).
- **`FSUSER:GetMediaType`** — the current media type (`MediaType::GameCard` for a card).
- **AM commands to get the inserted gamecard program ID** — e.g. `AM:GetProgramList` / `AM:GetTitleCount` filtered to `MediaType::GameCard`, and `AM:GetTitleID` variants (Home Menu uses `AM` to read the card's `ExeFS:/icon` SMDH for the banner, and the auto-boot flag).
- **The card's NCCH content** is read via the **FS GameCard archive** (archive ID `0x00000009`) — `FS:OpenArchive` with `GameCard` archive type, then `RomFS`/`ExeFS` access through `FS:OpenFileDirectly` with `MediaType::GameCard`.
- If the SMDH `ExeFS:/icon` has the **auto-boot flag** set, Home Menu boots the card game immediately instead of showing the menu.

So "card emulation" at the HLE level = make FS and AM report a card, and serve the game's CCI content through the FS archive interface.

## What the emulator currently does

`src/core/hle/service/fs/`:

- `archive.h` defines `enum class MediaType : u32 { NAND = 0, SDMC = 1, GameCard = 2 };` — the type exists.
- `archive.cpp` has an `IsGameCard()`-style helper (`return MediaType::GameCard`), and `fs_user.cpp` **references** `MediaType::GameCard` in several places (`GetSpecialContentIndexFromGameCard`, media-type checks around lines 1255, 1314, 1713-1719).
- **There is no GameCard archive implementation**: `FS:OpenArchive(0x00000009)` is not served; nothing backs a mounted card; `CardSlotIsInserted` / `GetCardType` return "not inserted"; AM has no card program ID. **The Home Menu therefore never shows a game card.**

Also relevant: the emulator loads games from `.cci`/`.3ds` files via `AppLoader_NCCH` (a `file_sys::NCCHContainer` on a host file) — that machinery is exactly what a GameCard archive could serve from.

## How to implement it (HLE approach — recommended)

1. **Add a "virtual game card" source**: when a `.cci`/`.3ds` (or `.cia`-extracted NCCH) file is selected/loaded, register it as the inserted card — e.g. a `GameCard` singleton in `core/file_sys/` holding an `NCCHContainer` for the current card image (title ID, icon SMDH, ROMFS, ExeFS, save).
2. **FS archive `0x00000009` (GameCard)**: implement `OpenArchive`/`OpenFile` for the GameCard archive type in `fs_archive.cpp` backed by the card container — mount `RomFS`, `ExeFS`, and a `SaveData`/gamecard-save archive backed by a writable file.
3. **FSUSER card commands**:
   - `CardSlotIsInserted` → return true when a card is registered (and support the "eject" case → false).
   - `GetCardType` → `CardType::CTR` (0) when a 3DS card is inserted.
   - `GetMediaType` → `MediaType::GameCard` when a card is inserted.
   - `GetSpecialContentIndexFromGameCard` — already present in `fs_user.h`, wire it to the card container (manual/update partitions).
4. **AM integration**: `AM:GetProgramList`/`GetTitleCount`/`GetTitleID` with `MediaType::GameCard` should return the card's title ID; `AM:GetProgramInfo`/`GetProgramInfos` should serve the card's exheader (needed for launching). Home Menu uses AM to read the card's `ExeFS:/icon` SMDH for the banner — the `smdh.cpp` loader already parses SMDH, so serve it from the card container.
5. **NS / launch path**: `NS:LaunchTitle` (or Home Menu's `APT:PrepareToStartApplication`) with the card title ID should boot the card's NCCH via the existing `AppLoader_NCCH` — the same path as booting a `.cci` directly today.
6. **Card save data**: a gamecard-save archive (per-title save file on SDMC or in the emulator's save dir) so games that save to the card work.

## Realism details (only if desired)

- **`FSUSER:CardSlotGetCardIFPowerStatus` / `CardSlotPowerOn`/`PowerOff`** — trivial state commands; return sensible values.
- **Card detection timing**: real consoles take ~280ms from power to first clock; Home Menu polls — no need to simulate.
- **Card update partition (CUP)**: retail cards carry a system-update CFA; Home Menu checks it and can refuse out-of-region cards ("This Game Card cannot be used"). For emulation, either serve a matching-region update partition or skip the check (the current Home Menu build is region-matched to the installed system files, so it's usually moot).
- **Auto-boot flag**: if the card's SMDH has the auto-boot flag, the real Home Menu boots the game immediately — emulating this is a nice touch for kiosk-style `.cci` files.
- **DS/DSi (TWL) cards**: `GetCardType` would return TWL type; actually running them needs TWL emulation (see `firm-booting.md` — out of scope).

## What the SDK's own code does with the card

- **FS client** (`sources/libraries/fs/CTR/MPCore/fs_UserFileSystem.cpp`) is the authoritative reference for the card save surface:
  - `FormatCtrCardSaveData(maxFiles, maxDirectories, isDuplicateAll)` → `FormatUserSaveDataForCheck(MEDIA_TYPE_CTRCARD, 0, ...)`
  - `GetCtrCardSaveDataFormatInfo(...)` → save-format query for the card
  - `MountCtrCardSaveDataForCheck(archiveName)` → **mounts the card save as a user save archive** (`MountUserSaveDataForCheck(MEDIA_TYPE_CTRCARD, 0, ...)`)
  - `GetCardType(CardType* pOut)` → `FileServer.GetCardType` — the `FSUSER:GetCardType` the Home Menu polls
  - `SetCtrCardLatencyParameter` / `SetFsCompatibilityInfo` / `ResetCardCompatibilityParameter` — card behavior knobs
  - `InitializeCtrFileSystem()` — the card filesystem init every game calls at boot
  - Card **ROM** content is read via the `GameCard` archive (0x00000009) through the same `fs` API (open RomFS/ExeFS with `MEDIA_TYPE_CTRCARD`).
- **`nn::cardspi`** (`include/nn/cardspi/cardspi_Api.h`): the ARM9-side SPI for the card's save flash (`GetCardSpiDeviceType`, `ReadAndWrite`, `ReadAndWriteWithHeaderAndFooter`) — only relevant if emulating the low-level bus, not the HLE FS path.
- **`nn::gd` / `sources/libraries/gd/CTR/gd_Common.cpp`**: the gamecard-device API used by Process9/FS on the ARM9 side — the HLE FS path replaces all of this.

## SDK material available

- `ref/ctr/include/nn/gd/` — official gamecard device API (identifies the command set the ARM9 side uses).
- `ref/ctr/sources/libraries/gd/CTR/gd_Common.cpp` — card state handling.
- `ref/3dbrew/wiki/Gamecards.md` — full command protocol + sample command trace (LEGO Star Wars III example) if you ever want to emulate the low-level protocol instead of the FS interface.
- `ref/3dbrew/wiki/Filesystem services.md`, `FS_OpenArchive.md`, `FS_GetMediaType.md`, `FS_CardSlotIsInserted.md`, `FS_GetCardType.md` — the exact FSUSER command semantics.

The **FS/AM HLE approach above is the correct one for this emulator** — it's how Citra-based emulators present a card without emulating the SPI bus, and it makes the Home Menu show the card banner, launch it, and save to it.
