# Booting `.firm` files in the emulator

Sources: `ref/3dbrew/wiki/FIRM.md`, `ref/3dbrew/wiki/Bootloader.md`, `ref/3dbrew/wiki/Bootrom.md`, `ref/ctr/include/nn/firmware/` (TWL/AGB), `src/core/loader/`.

## What a FIRM actually is

A FIRM is the firmware image the bootrom launches. It is **not an application** — it contains the ARM9 and ARM11 **kernels** plus the fundamental processes that set up the OS. Per `ref/3dbrew/wiki/FIRM.md`:

| OFFSET | SIZE | DESCRIPTION |
|---|---|---|
| 0x000 | 4 | Magic `'FIRM'` |
| 0x004 | 4 | Boot priority (normally 0) |
| 0x008 | 4 | ARM11 entrypoint |
| 0x00C | 4 | ARM9 entrypoint |
| 0x010 | 0x030 | Reserved |
| 0x040 | 0x0C0 (0x030×4) | Firmware section headers |
| 0x100 | 0x100 | RSA-2048 signature of the header's SHA-256 hash |

Each section header (0x30 bytes × up to 4):

| OFFSET | SIZE | DESCRIPTION |
|---|---|---|
| 0x000 | 4 | Byte offset |
| 0x004 | 4 | Physical load address |
| 0x008 | 4 | Byte size (0 = section absent) |
| 0x00C | 4 | Copy method (0 = NDMA, 1 = XDMA, 2 = CPU mem-copy) |
| 0x010 | 0x020 | SHA-256 hash of the section |

- For **NATIVE_FIRM / SAFE_MODE_FIRM**: ARM9 section = ARM9 kernel + Process9 NCCH; ARM11 sections = ARM11 kernel + the `sm`, `fs`, `pm`, `loader`, `pxi` process NCCHs.
- Sections loaded from NAND are plaintext; sections meant for SPI-flash/NTR-cartridge boot are encrypted (bootloader page).
- **New3DS** NATIVE_FIRM adds an extra crypto layer to the ARM9 binary: a plaintext loader at the end of the ARM9 image that derives console-unique keys from OTP and decrypts the real ARM9 binary with AES-CTR (keyslot 0x15/0x16; details in `FIRM.md`). **This makes raw New3DS NATIVE_FIRM .firm files undecryptable without the console's OTP** — you cannot just load the section and jump to it.

## The emulator's current boot model (why there's no FIRM loader)

`src/core/loader/` only has `3dsx`, `elf`, `ncch`, and `artic` loaders. The emulator **does not boot NATIVE_FIRM**:

- There is no ARM9 core and no Process9 emulation (`src/core/hw/aes/`, `ecc`, `rsa` are software crypto helpers used by HLE services, not a Process9 process).
- The ARM11 "kernel" is the emulator's own HLE kernel (`src/core/hle/kernel/`), not the real ARM11 kernel from FIRM.
- Boot = `System::Init` → `AppLoader::LoadExec` of one NCCH application (e.g. the Home Menu title from `system_titles.cpp`), then `PM`/HLE services fake the rest.
- `src/core/loader/ncch.cpp` explicitly rejects GBA Virtual Console titles (`ErrorGbaTitle`, `.CAA` magic check) — the AGB_FIRM path is not emulated.

So "booting a .firm" in this emulator today means one of the approaches below, **none of which is the real bootrom chain** (real Boot9 → FIRM signature check → ARM9 kernel → Process9 would require ARM9 emulation that does not exist).

## Approach A — Parse + run FIRM sections on the existing ARM11 core (research spike)

Feasible as a *proof of concept*, not as a replacement boot:

1. Add an `AppLoader_FIRM` (`src/core/loader/firm.cpp`) that parses the header (magic `'FIRM'`, section headers, SHA-256 verify each section).
2. For **Old3DS / plaintext sections** (e.g. TWL_FIRM ARM7/ARM9, or dev FIRMs): load the section bytes at their physical addresses via the memory interface and jump to the entrypoint with the existing `fastinterp`/`dynarmic` cores.
3. Limitations that will immediately bite:
   - The ARM11 sections contain the **real ARM11 kernel** — the emulator's kernel is HLE, so real-kernel code will hit unimplemented SVCs/MMU behavior. It cannot "take over" without implementing the real kernel's memory model.
   - The ARM9 side (kernel + Process9) needs an ARM9 core + ARM9 MMU/IO (AES, SHA, RNG, OTP, NAND, SPI, card) — none exist.
   - New3DS NATIVE_FIRM ARM9 crypto requires OTP-derived keys; without a real console dump the section cannot be decrypted.
   - The RSA signature (header +0x100) can't be validated without the bootrom public key, and is irrelevant for emulation anyway.

**Conclusion:** Approach A can only run *specific* firmware payloads that happen to be plaintext and not depend on real hardware registers. It will not boot the stock OS.

## Approach B — TWL_FIRM (DS/DSi mode) emulation

The SDK has the actual TWL firmware headers (`ref/ctr/include/nn/firmware/TWL/`: `firm/memorymap.h`, `ARM9/`/`ARM7/` mmap specs, `pxi.h`, `gx.h`, `lcfg.h`, `reboot.h`, `syscall.h`, plus `specfiles/ARM9-FIRM.ldscript.template` / `ARM7-FIRM.ldscript.template`) and the DS-mode GBA header (`ref/ctr/include/nn/firmware/AGB/format_rom.h` — `AgbHeader` with the 0x9C-byte Nintendo logo, title, gamecode, etc.).

What TWL_FIRM emulation would require:
- An **ARM7 core** (the DS's ARM7 CPU) — the emulator has no second ARM core of a different architecture.
- DS/DSi hardware: TWL PXI (`nn/firmware/TWL/pxi.h`), GX (2D), `lcfg`, RTC (`rtc.h`), AES (`aes.h`), and the TWL NAND layout.
- The whole DSi-mode boot chain (`TWL_FIRM` launched by NATIVE_FIRM, DS cart slot, DSi-ware).

This is essentially "implement a DS emulator inside the 3DS emulator" — the scope of melonDS. **Not practical as an incremental feature.**

## What the real boot code shows (from the leaked SDK)

Reading the actual system-module sources (`ref/ctr/sources/processes/`) pins down how a FIRM-booted system starts programs — useful context for any reimplementation:

- **loader** (`loader/ldr_LoaderImpl.cpp`, `ldr_Main.cpp`): the loader process exposes the `ldr:ro`/`ldr` port and does `LoaderImpl::CreateProcess(programHandle)` → `FillCache` → `GetMemoryRegion` (from kernel caps) → `ProcessImageMemory::Allocate` (text/rodata/data segments) → `LoadImage` (decompresses the ExeFS `.code`) → `CreateCodeSet` → `CreateProcess` via SVC. This is exactly what the emulator's `AppLoader_NCCH::LoadExec` reproduces at the HLE level (`src/core/loader/ncch.cpp`), so the HLE loader is already faithful to the real one.
- **PM** (`ProcessManager/pman_Main.cpp`, `pman_ProcessManager.cpp`): after boot, PM runs `LaunchStartupProcesses()` which reads `nn::os::GetReadOnlySharedInfo().firstMainProgram` and launches that one title with `LAUNCH_PROGRAM_FLAGS_LAUNCH_DEPENDENCY` from NAND. That is how NATIVE_FIRM hands off to the Home Menu — and it's exactly what the emulator does when it boots the Home Menu title. FIRM itself is only the kernel+fundamental-process bootstrap; the *real* process-launch logic lives in loader+PM, both of which the emulator's HLE reproduces.
- **CIA → FIRM** (`ApplicationManager/am_ApplicationManager.cpp`): `GetProgramInfoFromCiaFile` reads a program info from a CIA; FIRM install is `AM:InstallFIRM`/`InstallNATIVEFIRM` writing to the `firm0`/`firm1` NAND partitions (per 3dbrew), not something the emulator needs to boot.

**Implication:** the emulator's boot model is already equivalent to real `FIRM → loader → PM → firstMainProgram`. The only genuinely missing piece is the ARM9 side (see Approach B/A below).

## Approach C — The pragmatic path (what the emulator actually needs)

The emulator's goal for the Home Menu is met by **not** booting FIRM at all; instead the missing pieces are:

1. **A FIRM *reader* (not loader)** for tooling: parse/verify `NATIVE_FIRM`/`SAFE_MODE_FIRM`/`TWL_FIRM`/`AGB_FIRM` binaries, list sections, verify SHA-256 — useful for the system-files installer and for debugging. The `FIRM.md` header tables above are the complete spec; no new hardware emulation needed.
2. **AGB_FIRM / GBA Virtual Console**: currently `ncch.cpp` rejects `.CAA` GBA VC titles. Supporting GBA VC properly requires an ARM7/ARM9 AGB mode core — the honest scope note is "needs an embedded GBA core" (like melonDS's AGB mode or gpSP). The `format_rom.h` header documents the GBA ROM layout the AGB_FIRM hands to the GBA CPU.
3. **TWL (DSi) titles**: `am.cpp` has `TWL_TITLE_ID_FLAG` handling with a `TODO(PabloMK7)` noting TWL titles should live in TWL NAND — infra exists but the TWL runtime does not.

## Summary / recommended work

| Goal | Feasible | Effort |
|---|---|---|
| FIRM parser + section hash verification (tooling) | Yes, self-contained | Small (new `AppLoader_FIRM` or a `file_sys` parser) |
| Boot plaintext ARM11 FIRM sections on existing core | Research-only spike | Medium (won't boot real OS) |
| New3DS NATIVE_FIRM decryption | No (needs OTP) | — |
| TWL_FIRM (DS/DSi mode) | Requires full ARM7 + DS hardware emulation | Very large |
| AGB_FIRM (GBA VC) | Requires embedded GBA core | Very large |

The `ref/ctr` value here is documentation-grade: the TWL firmware mmap/ldscripts and the AGB `AgbHeader` are the exact reference for anyone attempting the ARM7/GBA cores, and the FIRM section/header tables are the exact spec for the parser.
