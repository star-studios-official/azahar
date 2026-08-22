# Missing functions inventory (complete gap analysis)

Method: stub counts and handlers were gathered by scanning the emulator's HLE services (`src/core/hle/service/*/`) for `(STUBBED)` / `(stubbed)` / `ReportUnimplementedFunction` sites. SDK ground truth for each area: `ref/ctr/sources/processes/*` (module sources) and `ref/ctr/include/nn/*` (client libs) — see the `leaked-sdk` skill.

## 1. HLE services — stubbed / missing commands

Stub-site counts per service (higher = more work to reach parity):

| Service | Stub sites | What is stubbed (headline items) |
|---|---|---|
| **BOSS** (`boss/`) | 50 | SpotPass: task registration, `RegisterTask`, task start/state, property handling, `GetNewArrivalFlag`, task service, storage access, `GetTaskOutput`, `SetTaskNotification` — nearly the whole service. Real module: `sources/processes/boss/`. |
| **NIM** (`nim/`) | 46 | Almost the **entire** `nim:u` table (`StartNetworkUpdate`, title download, tickets, seeds, system update, TSL/DTL XML) — all `(STUBBED)`. Real module: `sources/processes/nim/`; the fork's NUS downloader bypasses this. |
| **AM** (`am/`) | 25 | Content/`GetContentList`-type reads, ticket import, several media-type queries, `GetDeviceCert` (now synthesized), title export, `DeleteContents` variants. Real module: `sources/processes/ApplicationManager/`. |
| **CAM** (`cam/`) | 23 | Camera: `StartCapture`, `StopCapture`, frame control, `SetFrameRate`, white balance, exposure, `SetPhotoMode`, buffer control, `SetSleep` etc. Real module: `sources/processes/camera*`. |
| **FRD** (`frd/`) | 17 | Friends: profile/`GetFriendProfile`, presence `SetPresence`, `GetMyPresence`, `DecryptLocalFriendCode`, `GetFriendAttribute`, `GetNotificationList`, `GetFriendCount`. Real module: `sources/processes/friends/`. |
| **AC** (`ac/`) | 12 | Wi-Fi connection: `CreateConfig`, `Connect`, `GetConnectingInfrastructure`, `GetLastErrorCode`, `Close` — mostly faked success. Real module: `sources/processes/` (AC is part of NWM infra). |
| **FS** (`fs/`) | 11 | Mostly edge commands (`GetNandArchiveResource`, priority/`SetPriority`, `CheckArchive`, some secure-value paths). |
| **PTM** (`ptm/`) | 7 | `SetRtcAlarm`/`GetRtcAlarm`, pedometer writes, play history (see `leaked-sdk-findings.md` §2.3–2.4 for formats), `GetBatteryLevel` nuance. |
| **GSP** (`gsp/`) | 6 | GPU interrupts/framebuffer corner cases, `AcquireRight` edge handling. |
| **CECD** (`cecd/`) | 6 | StreetPass: `SendBox`, `ReceiveBox`, `OpenBox`, box data read (real format in `cec_MessageBoxAdmin.cpp`). |
| **APT** (`apt/`) | 6 | Applet corner cases: `CancelLibraryApplet`, `PrepareToStartLibraryApplet` edge handling, error messages. |
| **NWM** (`nwm/`) | 6 | UDS: `SetMaxSendDelay`, `Flush`, `SetProbeResponseParam`, `ScanOnConnection`, beacon-key stub. |
| **NFC** (`nfc/`) | 4 | Amiibo: `StartCommunication`/tag state — mostly surface. |
| **DLP** (`dlp/`) | 4 | Download play: `Initialize`, `GetServerState` etc. |
| **SM** (`sm/`) | 5 | Service manager corner cases (registration conflicts, `RegisterService` edge cases). |
| **ACT** (`act/`) | 2 | NNID account: `GetAccountInfo` block reading (now zero-filled), remaining deep account APIs. |
| **news/ldr_ro/qtm/hid** | 1 each | Notification corner cases, RO load, QTM, HID. |
| **MCU** (`mcu/`) | — | Service exists but is **nearly empty**: no LED/battery/power-button behavior. Real module: `sources/processes/mcu/`. |
| **SSL** (`ssl/`) | 0 | No stub sites because it's a thin success-returning shell — **effectively stubbed**; no real TLS. |

Notable implemented-but-incomplete: **SOC** (`soc_u.cpp`) implements BSD sockets with a few `getsockopt`/`setsockopt` options stubbed; **HTTP** (`http/`) works via host requests.

Every stub above has a real implementation to port from the SDK (`ref/ctr/sources/processes/`): BOSS → `boss/` (`boss_AcHandler.cpp`, `boss_NotificationManager.cpp`, `boss_NdmImpl.cpp`, `boss_PrivilegedImpl.cpp`), NIM → `nim/CTR/`, AM → `ApplicationManager/` (`am_ApplicationManager.cpp` has `GetProgramInfoFromCiaFile`, `GetProgramList`, `GetProgramInfos`, `GetProgramInfoFromCia`), CAM → `camera/` + `camera_dp2/`, FRD → `friends/`, AC → NWM infra (`nwm/CTR/nwm_InfraImpl.cpp`), CECD → `cecd/`, PTM → `ptm/`, MCU → `mcu/`, SSL → `ssl/` (`ssl_ConnectionServer.cpp`, `ssl_CertStoreServer.cpp`), applets → `sources/libraries/{swkbd,erreula,mii}/` (client libs) + `include/nn/applet/CTR/`. Note: the library applets themselves run inside the Home Menu process on real hardware — the emulator's HLE applets are the right approach; the SDK's `applet_*` headers define the exact IPC contract.

## 2. CPU / ARM gaps

- **ARM11 (main CPU)**: `dynarmic` JIT + `fastinterp` + `dyncom` cover the ARM11 feature set well; gaps are behavioral (cycle timing in `arm_tick_counts.cpp`, `CP15` edge cases in `arm_dynarmic_cp15.cpp`, VFP rounding corner cases). No known missing instructions at the ISA level — Dynarmic implements ARMv6K.
- **ARM9 (security CPU)**: **entirely missing** — no ARM9 core, no MMU, no Process9 process. Consequences: no real FIRM boot (see `firm-booting.md`), no real card/NAND crypto at the hardware layer (HLE `aes`/`rsa`/`ecc` software crypto substitutes), no OTP.
- **ARM7 (DS/DSi mode)**: **missing** — required for TWL_FIRM (DS/DSi-ware) and DS gamecards.
- **GBA CPU**: **missing** — required for AGB_FIRM / GBA Virtual Console (`.CAA` titles currently rejected in `ncch.cpp` with `ErrorGbaTitle`).

## 3. GPU (PICA200) gaps — `src/video_core/`

- **Rendering**: OpenGL/Vulkan/software rasterizers exist and are broadly complete. Known inaccuracies:
  - Geometry shader (GS) program/swizzle edge cases (`pica_core.cpp` logs errors for invalid GS/VS offsets; `TODO(PabloMK7)` comments on register-masking accuracy).
  - `vs.uniform_setup.set_value` register masking (explicit TODO).
  - LUT/texture corner cases, fragment lighting precision, and the remaining PICA quirks tracked in `video_core/pica/`.
- **Not emulated at all**: the **LCD/display pipeline** (`regs_lcd.h` exists as registers; no real display timing), 3D slider/parallax (stubbed via setting), and the **`gd` PICA library behaviors** in `ref/ctr/sources/libraries/gd/CTR/` (lighting/combiner/fog details) — useful as the official spec for closing rendering gaps.

## 4. Applets — `src/core/hle/applets/`

Present: **erreula** (error display), **mii_selector**, **swkbd** (software keyboard), **mint** (Mii network tool).
Missing / stubbed library applets:
- **Photo viewer** (`phtsel`) — needed by camera/Gallery titles.
- **Software keyboard variants** and the **"notification" applet** used by Home Menu notifications.
- **`NS` applet-side helpers** (Home Menu ↔ library applet messaging edge cases in `apt`).
- The full `applet` command surface in `ref/ctr/include/nn/applet/CTR/` (applet IDs, `APPLET_ID_NONE`/`0x300`/`0x501`, message callbacks) for parity.

## 5. Firmware / system modules

- **FIRM boot** — no FIRM loader at all; see `firm-booting.md` (parser feasible; real boot needs ARM9).
- **TWL_FIRM (DS/DSi mode)** — missing (ARM7 + TWL hardware). `ref/ctr/include/nn/firmware/TWL/` has the full reference.
- **AGB_FIRM (GBA VC)** — missing; `.CAA` titles rejected (`ncch.cpp:133`). `ref/ctr/include/nn/firmware/AGB/format_rom.h` documents the ROM header.
- **process9** — missing (see CPU §2); its responsibilities (title install/delete, ticket verification, seed crypto) are partially reimplemented in HLE (`file_sys/`, `hw/aes/`, `hw/rsa/`, `hw/ecc/`).

## 6. Installing CIAs / title management

- **CIA parsing** exists: `core/file_sys/cia_container.cpp`, `ticket.cpp`; **AM import** exists (`CIAFile`, `ImportTitle` state machine, encrypted-CIA authorization with a `compress_cia_installs` option).
- Gaps:
  - `AM` content-level commands stubbed (25 sites, see table) — e.g. content list reads used by some installers/checkers.
  - **NIM-based install** (the official network-install path) is entirely stubbed — the fork routes around it with its own NUS downloader.
  - TWL title placement: `am.cpp:1355` `TODO(PabloMK7)` — TWL titles should install to TWL NAND.
  - Delete/uninstall paths for DLC/updates are partial.

## 7. Networking / linking — see `official-networking.md`

Summary: UDS local multiplayer works over LAN; SOC works; SSL is a shell; AC states are faked; friends servers/NNID/eShop/SpotPass are not emulated; StreetPass (CECD) box mechanics stubbed (real format available); Download Play (DLP) stubbed.

## 8. Card emulation — see `gamecard-emulation.md`

Summary: `MediaType::GameCard` exists in `fs/archive.h` but **no GameCard archive (0x00000009)**, no `CardSlotIsInserted`/`GetCardType` truth, no AM card program ID — the Home Menu never sees a card. Full HLE implementation plan in that doc.

## 9. Misc / system services

- **`mcu`** — nearly empty (no LED/battery/power-button simulation).
- **`pxi`** — ARM11↔ARM9 IPC: HLE shim exists; real PXI not emulated (no ARM9).
- **`qtm`** (face tracking) — 1 stub; N3DS feature.
- **`mvd`** (video decoding) — mostly present but video-codec edge cases stubbed.
- **`csnd`** (audio) — present; DSP LLE optional.
- **Home Menu-specific**: notification count, themes (`Home Menu__Themes.md`), and the account/`act` deep APIs.

## Priority ranking (impact ÷ effort)

1. **Game card emulation** (FS archive + AM card ID + FSUSER commands) — unblocks a huge Home Menu feature; mostly wiring existing NCCH/SMDH code.
2. **PTM pedometer + play history + RTC alarm** — small, documented formats (`leaked-sdk-findings.md` §2.3–2.6).
3. **Un-stub NWM UDS + AC states** — small, makes more titles' online menus behave.
4. **SSL real TLS** — medium; enables HTTPS titles.
5. **CECD StreetPass boxes over LAN** — medium; real format available.
6. **BOSS/NIM/FRD/CAM/AM command completion** — large; each needs the real module source ported to HLE semantics.
7. **MCU, applets (photo viewer), QTM** — small-medium; UI polish.
8. **TWL/AGB/FIRM/ARM9** — very large; see `firm-booting.md` (needs whole new CPU cores).
