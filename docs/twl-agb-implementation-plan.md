# TWL_FIRM (DS/DSi) & AGB_FIRM (GBA) Implementation Plan

Status: planning. This doc is the roadmap for running `.nds` / `.dsi` / `.gba`
ROMs on Azahar by emulating the 3DS's TWL (DS-mode) and AGB (GBA-mode)
subsystems. Grounding references: `ref/melonDS`, `ref/mgba`, `ref/ds_bios`,
`ref/gba_bios`, the dsibrew archive at `ref/ds_bios/dsi/dsibrew/`, and 3dbrew.

## 1. How the real hardware does it

A 3DS runs DS games by booting **TWL_FIRM** (title `0x0004013000000102` O3DS /
`0x0004013020000102` N3DS, already listed in `src/core/system_titles.cpp`).
TWL_FIRM is a FIRM image containing bootloaders + processes that:

1. Switch the console into **TWL (DS) mode** — the ARM11 behaves as the DS's
   ARM9, a second ARM7 (the "TWL ARM7") is instantiated, and the hardware
   (GPU, timers, DMA, SPU) is reconfigured to DS behavior.
2. Boot the **TWL System Menu** (DS/DSi menu) or directly launch the inserted
   **TWL GameCard** (an `.nds`/`.dsi` ROM).
3. On exit, reboot back into CTR (3DS) mode via the power-management hardware.

GBA games use **AGB_FIRM** (`0x0004013000000202` O3DS / `0x0004013020000202`
N3DS): the 3DS boots a slimmed ARM9 core that acts as the GBA CPU, loads the
GBA BIOS + ROM from the **GBA NAND partition** (`agbsave.bin` + installed
`00040000xxxxxxxx` "virtual console" titles), and the ARM11 becomes a
controller/peripherals coprocessor (the "AgbBackdoor" AXI interface).

**Consequence for the emulator:** Azahar's core is CTR-only (ARM11 MPCore +
PICA200 HLE). It cannot execute DS/GBA code. The only sane implementation is
to embed **melonDS** (a complete DS/DSi emulator) and **mGBA** (a complete GBA
emulator) as subsystem cores and run them inside the emulation loop,
replacing the CTR core while a TWL/AGB title is active.

## 2. What we already have / changed so far

- `src/core/system_titles.cpp` already declares TWL_FIRM/AGB_FIRM/TWL-system
  title IDs and the TWL NAND high words (`0x00048005` TWL system apps,
  `0x0004800F` TWL data archives).
- `.nds`/`.dsi`/`.gba` are now recognized by the iOS game library
  (`src/ios/AzaharApp/Models/Game.swift`) and `.nds`/`.dsi` are eligible for
  "Load as Game Card" (`isGameCardEligible`).
- `Core::System::InsertCartridge` (`src/core/core.cpp`) now accepts `.nds`,
  `.dsi`, `.gba` by extension so `az_insert_cartridge()` reports success and
  `System::GetCartridge()` is non-empty (Home Menu card-present checks).
- A **HOME button** was added to the iOS touch controls
  (`src/ios/AzaharApp/Views/TouchControlsView.swift`) sending `AZ_BUTTON_HOME`.

None of that runs TWL code yet — it only makes the files visible. The rest of
this doc is the actual implementation.

## 3. Reference assets (all present in `ref/`)

| Asset | Path | Used for |
|---|---|---|
| NDS ARM7/ARM9 BIOS | `ref/ds_bios/biosnds7.rom`, `biosnds9.rom` | DS-mode boot (melonDS `SetARM7BIOS`/`SetARM9BIOS`) |
| DSi BIOS | `ref/ds_bios/BIOSDSI7.ROM`, `BIOSDSI9.ROM`, `bios7i.bin`, `bios9i.bin` | DSi-mode boot |
| DSi NAND | `ref/ds_bios/dsi/NAND/` | TWL System Menu, DSiWare, TWL NAND layout |
| GBA BIOS | `ref/gba_bios/gba_bios.bin` | AGB mode (mGBA `mGBAInit`/`GBASetBIOS`) |
| melonDS source | `ref/melonDS/src/` | DS/DSi core (`NDS.cpp/h`, `NDSCart.*`, `GPU2D_Soft.*`, `SPU.*`, `DSi_NAND.*`) |
| mGBA source | `ref/mgba/` | GBA core (`src/gba/`, `include/mgba/`) |
| dsibrew archive | `ref/ds_bios/dsi/dsibrew/` | NDS/DSi internals (Bootloader, ARM9 OS, Card hardware, NAND) |
| 3dbrew | `ref/3dbrew/wiki/FIRM.md`, `Bootloader.md`, `NAND.md`, `AM_GetTWLPartitionInfo.md` | TWL/AGB partition + FIRM layout |

## 4. Architecture

### 4.1 Build integration

- Add `externals/melonDS` and `externals/mgba` as CMake subdirectories (or
  git submodules pinned to the `ref/` snapshots) exposing static libs
  (`melonDS`, `mgba`). melonDS already has a CMake build
  (`ref/melonDS/CMakeLists.txt`); mGBA has `ref/mgba/CMakeLists.txt`.
  Gate both behind a CMake option `CITRA_ENABLE_TWL_AGB` so CTR-only builds
  (and CI) are unaffected until the ports are stable.
- iOS: add both libs to the Xcode project via the existing `citra_core`
  target's sources (src lists are explicit per `AGENTS.md`), and make sure the
  BIOS/NAND files are bundled as app resources (they must not be committed to
  git; load from the app Documents dir, mirroring how system files are
  imported today).

### 4.2 The subsystem interface

New `src/core/twl/` and `src/core/agb/` trees with a common shape:

```
src/core/twl/
  twl_system.h/.cpp    // owns melonDS::NDS, BIOS/NAND loading, frame pump
  twl_cartridge.h/.cpp // .nds/.dsi loader → melonDS NDSCart
  twl_host.h/.cpp      // CTR-side glue: input, audio, framebuffer, timers
src/core/agb/
  agb_system.h/.cpp    // owns mGBA GBA instance
  agb_host.h/.cpp      // CTR-side glue for GBA
```

`Core::System` gains a pointer to an optional `TWLSystem`/`AGBSystem` and a
`Core::System::RunLoop()` branch: when the active title is a TWL/AGB title,
drive `twl.NDS().RunFrame()` (melonDS's `u32 NDS::RunFrame()`, NDS.h:415)
instead of the ARM11 frontend, then hand the rendered framebuffer + audio
samples to the frontend.

### 4.3 Boot paths

**Path A — direct `.nds` launch (fastest to ship):** launching an `.nds` from
the library switches the core into "TWL subsystem" mode immediately: load
`biosnds7.rom`/`biosnds9.rom` (or DSi BIOSes + NAND for `.dsi`), insert the
ROM via `NDSCart`, and run melonDS with the DS "direct boot" flag (melonDS
supports skipping the DS firmware menu). This gives playable DS without
bootstrapping TWL_FIRM.

**Path B — real TWL_FIRM via the Home Menu (matches hardware):** the Home Menu
sees the inserted `.nds` as a TWL GameCard (Section 5), and launching it boots
`TWL_FIRM` from the TWL NAND (`nand:/twl/title/00040130/...`), which then
chain-boots melonDS into DS mode. This is the "correct" path but requires the
TWL NAND + System Menu images and the TWL_FIRM FIRM parser. Do this in a later
phase; Path A unblocks the feature.

**Path C — AGB:** mGBA needs the GBA BIOS (`gba_bios.bin`) and a GBA ROM. On
3DS, GBA games are installed NAND titles run under AGB_FIRM; for the emulator,
simplest is direct `.gba` launch through mGBA (mGBAInit + mGBALoadROM +
mGBARunFrame). Optionally wrap it as an "installed" `00040000xxxxxxxx` title
so it appears on the Home Menu like a VC game.

### 4.4 Frame loop, audio, input

- **Video:** melonDS `GPU2D`/`GPU3D` render to its own buffers; expose the
  composed screen(s) to the iOS `MetalView` through the same surface path the
  CTR renderer uses (see `src/ios/AzaharBridge/EmuWindowIOS.mm`), scaling DS
  (256x192) or DSi (256x192, two screens) to the display. mGBA renders
  240x160 RGB32 via `mGBAGetPixels`.
- **Audio:** melonDS SPU outputs at 32768 Hz; mGBA at 32768 Hz. Feed into the
  existing `AudioCore` sink (the same cubeb/openal backend the CTR audio uses)
  so routing/volume/mic code is reused. DS mic input can be bridged to the
  existing OpenAL capture input later.
- **Input:** reuse `AZ_BUTTON_*`/`AZ_STICK_*`; map 3DS buttons to DS buttons
  (A/B/X/Y, D-Pad, Start/Select, L/R, touchscreen via the touch overlay
  position on the bottom screen). GBA maps A/B/L/R/Start/Select/D-Pad.
- **Speed/timing:** melonDS `RunFrame` returns lag/frame status; run at the
  emulator's target speed with the existing frame limiter.

### 4.5 Save data

- DS: melonDS `NDSCart` exposes save memory
  (`NDS::GetNDSSave()/GetNDSSaveLength()`, NDS.h:372-374); persist to
  `sdmc:/Azahar/saves/<rom>.sav` with the same naming convention as CTR saves.
- GBA: mGBA `mGBAGetSaveData`/`mGBASetSaveData`; persist `.sav` similarly.

## 5. Home Menu game-card integration (TWL card)

The Home Menu shows an inserted DS game card via the CTR gamecard path
(`NS::LaunchTitle` with `MediaType::GameCard` + TWL title id, and the
gamecard-info commands on `ns:s`). With the current stubs the card is
invisible. Required steps:

1. `NS::LaunchTitle` (ns_s.cpp) — when `media_type == GameCard` and the
   cartridge path ends in `.nds`/`.dsi`, route to the TWL subsystem (Path B)
   instead of `Loader::GetLoader` (which cannot parse `.nds`).
2. `AM`/`NS` gamecard info commands (`NS_GetGameCardInfo`, card title/icon):
   parse the `.nds` header (DSi/NDS cart header at ROM offset 0, per
   `ref/ds_bios/dsi/dsibrew/DSi cartridge header.md`) and answer with the
   game title/icon so the Home Menu renders the card.
3. Whitelist: the TWL Card Whitelist archive (`0x0004800F` / "DS Card
   Whitelist") gates DS games on real hardware; in HLE just accept all cards
   and log.
4. After exiting DS mode, reboot back to CTR mode (TWL_FIRM normally does this
   via the power-management register writes; the subsystem wrapper should
   simply destroy the TWLSystem and resume the CTR core's Home Menu process).

## 6. Phased milestones

| Phase | Scope | Exit criteria |
|---|---|---|
| **P1 — DS core in a box** | Add melonDS to the build; `src/core/twl/` with BIOS loading from `ref/ds_bios`; direct `.nds` launch from the iOS library; frame + audio to frontend | A `.nds` game boots and plays on iOS |
| **P2 — polish** | DS input mapping, touchscreen, saves, savestates, .dsi + DSi NAND | DS/DSi games playable with saves |
| **P3 — TWL game card** | `.nds` via long-press "Load as Game Card" visible in Home Menu; `NS::LaunchTitle` TWL routing | Home Menu shows and launches a DS card |
| **P4 — AGB core** | Add mGBA to the build; `src/core/agb/`; direct `.gba` launch; input/audio/saves | GBA games playable |
| **P5 — AGB on Home Menu** | Install `.gba` as `00040000xxxxxxxx` VC title or AGB card; boot via AGB_FIRM | GBA games appear on Home Menu |
| **P6 — full TWL_FIRM** | Boot actual TWL_FIRM from TWL NAND; TWL System Menu; exit-to-CTR reboot | Hardware-accurate DS/DSi behavior |

## 7. Risks / notes

- **License/headers:** both melonDS (GPLv3) and mGBA (MPL2.0) are copyleft;
  consistent with this project's GPLv2 core but the iOS app distribution must
  keep the subsystem objects separate (static libs) so the app store build
  stays clean. Check `AI-POLICY.md` before upstreaming.
- **TWL NAND is large:** DSi NAND dumps (hundreds of MB) shouldn't ship in the
  repo; load from the app Documents dir like other system files, and only P3+
  needs it (Path A needs only the two BIOS files, ~4KB each).
- **Performance:** melonDS with JIT (ARMJIT) can't use `MAP_JIT` on iOS (see
  the existing "JIT is NOT available" log line) — the interpreter will run
  DS games at reduced speed on older iPhones; acceptable for P1.
- **Stub traps:** DS games issue `svcGetSystemInfo`/DS-specific syscalls only
  inside melonDS's own ARM cores, so the CTR kernel is untouched while in
  subsystem mode — the subsystem must *pause* the CTR kernel (stop scheduling
  ARM11 threads) rather than coexist with it.

## 8. Immediate next steps

1. Vendor `ref/melonDS` into `externals/melonDS` and get a minimal static-lib
   build compiling for iOS (gate behind `CITRA_ENABLE_TWL_AGB`).
2. Implement `src/core/twl/twl_system.{h,cpp}` with `LoadBIOS`, `InsertCart`,
   `RunFrame` and a `TWL_HOST` video/audio callback pair, wired into
   `Core::System` behind the option.
3. Add the P1 launch path to the iOS bridge (`az_run` branching on `.nds`).
