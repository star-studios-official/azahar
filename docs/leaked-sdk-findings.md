# Leaked 3DS SDK Findings — Reimplementable Improvements

Source: `ref/ctr` (leaked Nintendo 3DS SDK, `ctr_firm` is only the boot-ROM/FIRM build tree and is NOT relevant to app-level emulation).

The SDK contains the **actual source code of every system module** (`ref/ctr/sources/processes/`) and the **official client libraries** (`ref/ctr/include/nn/`). This is the ground truth behind every behavior the emulator currently approximates. Below is an organized catalogue of concrete, reimplementable items, ranked by value.

---

## 1. Already mined (committed fixes)

- **ACT module boot fatal (desc 351)** — `processes/act/util/act_SystemInfoManager.cpp` shows the module reads console device info at boot (`AM:GetDeviceCert`, `AM:GetDeviceID`, `CFG:SecureInfoGetSerialNo`, `AM:GetProgramInfos` for NVer). Any failure → `ResultDeviceInfoReadError`. Fixed by synthesizing device identity in the emulator (`am.cpp`, `cfg.cpp`).
- **ACT save data is self-initializing** — `AccountManager::CreateSingleton` creates its own KVS text files ("The administrative file not found. Created."). The fabricated binary `00010038` save data was removed.
- **FRD module tolerates missing seed/serial** — no boot fatal.
- **CEC message box format** — the emulator's reverse-engineered `CecMessageHeader` matches the SDK's official `cec_MessageBox.h` layout (validation point, no change needed).

---

## 2. Highest value: save-data / file formats (exact, documented)

### 2.1 NEWS database and notification files — `processes/news/`, `include/nn/news/CTR/`

The emulator's `news.cpp` has its own `NewsDBHeader`; the SDK documents the **official on-disk format** (`news_SystemImpl.cpp`, `news_File.h`):
- Notification header magic, per-record layout, and the database header structure (`FileHeader` / `DataHeader` in `include/nn/news/CTR/system/news_*.h`).
- The 0x10-byte "database info" block and the notification record layout (`news_UserTypes.h` — flags, timestamp, unread, optional title/body).

**Reimplement:** Replace the emulator's reverse-engineered NEWS DB write path with the official layout so save files are byte-compatible with real consoles and with homebrew that reads them. Also gives the exact `GetTotalNotifications` / `SetNotificationHeader` semantics.

### 2.2 CECD box structure — `processes/cecd/CTR/cec_MessageBoxAdmin.cpp`, `cec_Const.h`

Full official constants: `CEC_ARCHIVE`, `CEC_DIRNAME` values, per-box capacity limits, and the box metadata (`CecOutBoxIndexHeader` / `InBoxIndexHeader`). The emulator's header already matches; **the missing piece is the box index/management semantics** (max message count, oldest-first eviction, and the `MESSAGE_MAGIC` values the SDK uses: `0x6060`/`0x6868` etc.).

**Reimplement:** exact eviction/limits in the HLE CECD so `cec:u` box writes behave like hardware.

### 2.3 PTM pedometer — `processes/ptm/`, `include/nn/ptm/CTR/ptm_Types.h`

The official `PedometerHistoryHeader` / per-day step records are in the SDK. The emulator's `PTM:GetStepHistory` is a stub returning synthetic data.

**Reimplement:** real `Pedometer` file read/write in the `ptm:sys` savegame (`pedometer.db`) — enables the Home Menu's Step Counter and step-based games (e.g. Play Coins) to work from a persistent store.

### 2.4 PTM play history — `include/nn/ptm/CTR/`, play-history save file

`struct PlayEvent` (title ID, timestamps, play time) documented in the SDK. `ptm:play`'s `GetPlayHistory` is currently a stub.

**Reimplement:** persist real play sessions (start/stop events) to the play-history save file and serve them back — gives the Activity Log accurate data.

### 2.5 CFG config savegame — `include/nn/cfg/CTR/detail/cfg_DataStructures.h`, `cfg_Keys.h`

The official config-block key IDs and the savegame layout (`ConfigSavegame`). The emulator's CFG uses `ConsoleUniqueID2BlockID` etc. — verify the block IDs against `cfg_Keys.h` and fix any drift.

### 2.6 PTM alarm — `processes/ptm/CTR/ptm_Main.cpp`, `struct RtcAlarmParam`

Exact RTC alarm structure (alarm time + enable/disable flags) for `PTM:SetRtcAlarm` / `GetRtcAlarm` — currently stubbed.

---

## 3. High value: service semantics from module sources

### 3.1 BOSS notification / LED behavior — `processes/boss/boss_NotificationManager.cpp`

The official notification-manager logic (how BOSS decides the notification LED state from task results). The emulator's HLE BOSS stubs these; the SDK shows what a "task finished" notification should look like.

### 3.2 NEWS system impl — `processes/news/CTR/news_SystemImpl.cpp`

The full notification-storage engine behind `news:u`/`news:s` — storage limits, oldest-first deletion ("Not enough space available. Deleting oldest notification ID"), and the exact `news:sys` IPC behavior. The emulator's `news.cpp` reimplements a subset; align with this.

### 3.3 PTM power manager — `processes/ptm/CTR/ptm_Main.cpp`

Battery/shutdown/`SetLegacySleep`/`SetSleep` semantics — what the module actually does on `PTM:SleepSystem`, `PTM:ShutdownAsync` (LED sequence, shell-state checks, `SleepCheckOnEnableSleep` enum from `applet_Parameters.h`).

### 3.4 NIM — `processes/nim/`

Full NIM (network install) module source: title-download state machine, ticket handling, and the NIM save data. The emulator's `nim_u.cpp` stubs nearly everything — the SDK gives exact command semantics for the pending/registered-title APIs.

### 3.5 MCU — `processes/mcu/` (and the `mcu` library)

The emulator's `mcu` HLE service is nearly empty. The SDK has the real MCU firmware-interface code (LED patterns, button combos, battery). Reimplementing `mcu:u`/`mcu::Hwc` gives Home Menu features that currently no-op.

---

## 4. Medium value: crypto / ARM9 (process9)

`processes/process9/` contains the security-module source (title install/delete, ticket handling, seed crypto). Direct reimplementation is not realistic (it runs on ARM9 hardware), but the SDK documents:
- The exact **title-install validation order** (content hash chain, ticket signature checks) the emulator approximates in `loader/` and its NUS downloader.
- The **certificate chain structure** used by `AM:GetDeviceCert` (already partially used: `BuildECC` + `GenerateKeyPair` produced a valid 384-byte cert).

## 5. Medium value: SSL cert store — `processes/ssl/`

The SDK shows the SSL module's CA-cert store management (`SSLC:AddTrustedRootCert` etc.). The emulator's `ssl` service is mostly stubbed; the SDK gives the exact cert-count/limits and behavior for the `SOCU` integration.

## 6. Lower priority / context only

- **Applet library** — `include/nn/applet/CTR/` (applet IDs, `APPLET_ID_MAX`, `APPLET_ID_NONE` vs `0x300` application / `0x501` errdisp) — useful for verifying the emulator's `apt` service applet-ID handling.
- **FS library** — `include/nn/fs/CTR/fs_*.h` archive-type constants — verify the emulator's `fs` archive-type enums match.
- **Mii StoreData** — `include/nn/mii/mii_StoreData.h` — the emulator's Mii database format; verify field offsets.

---

## Recommended implementation order

1. **PTM pedometer + play history** (`ptm:sys`/`ptm:play`) — self-contained, documented formats, immediately user-visible (Step Counter, Activity Log).
2. **NEWS DB official format** — align the existing implementation's on-disk layout.
3. **CECD box limits/eviction** — small, exact semantics from `cec_MessageBoxAdmin.cpp`.
4. **MCU LED/battery basics** — unblocks Home Menu battery/notifications UI.
5. **NIM semantics** — for the fork's NUS downloader/encrypted-CIA path to report accurate progress/state.

All formats above are quoted directly from `ref/ctr` sources; the emulator changes for items 2–6 are additive and do not touch the LLE boot path fixed earlier.
