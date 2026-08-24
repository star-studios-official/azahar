# Stubbed & Unimplemented Services Wiki

> **Purpose**: Catalog every stubbed (`STUBBED`) and unimplemented (`nullptr`) IPC command across the Azahar emulator, categorized by crash risk. This helps prioritize what to implement to fix Home Menu, game loading, networking, and general stability.

---

## Table of Contents

1. [Critical: Known Crash-Causing Stubs](#1-critical-known-crash-causing-stubs)
2. [High Priority: Home Menu Services](#2-high-priority-home-menu-services)
3. [High Priority: Networking Services](#3-high-priority-networking-services)
4. [Medium Priority: Game Support Services](#4-medium-priority-game-support-services)
5. [Low Priority: Rarely Called Stubs](#5-low-priority-rarely-called-stubs)
6. [SVC Table Unimplemented Entries](#6-svc-table-unimplemented-entries)
7. [Per-Service Detailed Breakdown](#7-per-service-detailed-breakdown)

---

## 1. Critical: Known Crash-Causing Stubs

These are commands that **will crash the emulator** if called by the Home Menu or a running game.

### ✅ FIXED: `nwm::SOC` CMD 0x0009 — `GetMbufPoolInformation`

| Field | Value |
|-------|-------|
| **Service** | `nwm::SOC` |
| **CMD ID** | `0x0009` |
| **Status** | ✅ **FIXED** (commits pending) |
| **Crash** | Home Menu PANIC at PC `0x0010313C`, unmapped Read32 `0x10000FEC` |
| **Root Cause** | Socket sysmodule's first command; NWM module reads unmapped memory on failure |
| **Source** | `src/core/hle/service/nwm/nwm_soc.cpp` |
| **3DBrew Ref** | [NWM_Services#nwm::SOC](https://www.3dbrew.org/wiki/NWM_Services) |
| **Leaked SDK** | `ref/ctr/sources/libraries/nwm/CTR/nwm_Socket.cpp` — `Socket::GetMbufPoolInformation` |
| **Response Format** | `cmdreply[2]=sharedmem_size(0x22000), cmdreply[4]=shmem_handle, cmdreply[5]=event_handle` |

---

## 2. High Priority: Home Menu Services

These services are used during Home Menu boot and operation. Stubs here can cause freezes, black screens, or silent failures.

### `SRV` (Service Manager)

| CMD | Name | Status | Notes |
|-----|------|--------|-------|
| 0x0004 | `UnregisterService` | nullptr | Called during service cleanup. **Low crash risk** — only called during shutdown. |
| 0x0006 | `RegisterPort` | nullptr | Legacy Luma3DS compat. Only used by old homebrew. |
| 0x0007 | `UnregisterPort` | nullptr | Same as above. |
| 0x0008 | `GetPort` | nullptr | Legacy. Modern titles use `GetServiceHandle`. |
| 0x000B | `ReceiveNotification` | nullptr | Deprecated notification path. |

**Stubbed functions:**

| Function | Risk | Notes |
|----------|------|-------|
| `RegisterClient` | ✅ Works | Returns success but PID tracking is basic. |
| `GetServiceHandle` | ✅ Works | Supports delayed service wakeup. |
| `RegisterService` | ✅ Works | Full implementation. |
| `Subscribe` / `Unsubscribe` | ⚠️ Stubbed | Notifications may not fire correctly for Home Menu. |
| `ReceiveNotification` | ⚠️ Stubbed | Returns success but no actual notification delivery. |

---

### `APT` (Application Lifecycle)

**File**: `src/core/hle/service/apt/apt.cpp`

All three variants (`apt:A`, `apt:S`, `apt:U`) share the same base implementation. The `nullptr` commands in the `apt` module table (IDs 0x0001–0x001F) are reserved/internal and not called via IPC.

**Stubbed functions in APT:**

| Function | Risk | What It Does | Impact |
|----------|------|-------------|--------|
| `AppletUtility` | ⚠️ Stubbed | Returns random data for some operations | Home Menu uses this for applet communication. May cause visual glitches. |
| `SetFilterKeys` | ⚠️ Stubbed | Filters HID input for applets | Minor — input might leak to background applets. |
| `GetProgramInfo` | ✅ Works | Returns title ID and media type | Critical for game launching. |
| `PreloadLibraryApplet` | ⚠️ Stubbed | Preloads applet modules | Applets may load slower but still work. |
| `PrepareToStartLibraryApplet` | ⚠️ Stubbed | Prepares library applet for start | Same as above. |
| `StartLibraryApplet` | ✅ Works | Starts a library applet (e.g. keyboard) | Critical. |
| `SendCaptureBufferInfo` | ⚠️ Stubbed | Shares capture buffers between applets | May affect screenshot applet. |
| `ReceiveCaptureScreen` | ⚠️ Stubbed | Receives screenshot data | Screenshot applet won't work. |

**nullptr APT commands (apt:A/S/U):**

These are internal NWM module commands, not called by user applications:

| CMD | Name | Risk | Notes |
|-----|------|------|-------|
| 0x0001 | `Initialize` | ✅ Works | Critical for app lifecycle |
| 0x0002 | `GetSharedMemoryHandle` | ✅ Works | Returns shared memory for applet state |
| 0x0003 | `Unknown` | ❌ nullptr | Internal — not called by apps |
| 0x0004 | `Enable` | ✅ Works | |
| 0x0005 | `Disable` | ✅ Works | |
| 0x0006 | `GetAppletManInfo` | ✅ Works | |
| 0x0007 | `GetAppletInfo` | ✅ Works | |
| 0x0008 | `IsRegistered` | ✅ Works | |
| 0x0009 | `GetSpecificAppletManInfo` | ✅ Works | |
| 0x000A | `IsLibraryAppletFinished` | ✅ Works | |
| 0x000B | `SendParameter` | ✅ Works | |
| 0x000C | `ReceiveParameter` | ✅ Works | |
| 0x000D | `GrabLock` | ✅ Works | |
| 0x000E | `Prelaunch` | ⚠️ Stubbed | Some titles use this to pre-launch game cards |
| 0x000F | `IsRegisteredBackUpOnly` | ✅ Works | |
| 0x0010 | `SetAppletProgramPosition` | ✅ Works | |
| 0x0011 | `OrderToClose` | ✅ Works | |
| 0x0012 | `PrepareToStartApplication` | ✅ Works | |
| 0x0013 | `StartApplication` | ✅ Works | Critical — launches the actual game |
| 0x0014 | `WakeupApplication` | ✅ Works | |
| 0x0015 | `CancelApplication` | ✅ Works | |
| 0x0016 | `Unknown16` | ❌ nullptr | |
| 0x0017 | `Unknown17` | ❌ nullptr | |
| 0x0018 | `Unknown18` | ❌ nullptr | |
| 0x0019 | `GetStartupAttempt` | ✅ Works | |
| 0x001A | `Unknown1A` | ❌ nullptr | |
| 0x001B | `Unknown1B` | ❌ nullptr | |

---

### `GSP::GPU` (Graphics)

**File**: `src/core/hle/service/gsp/gsp_gpu.cpp`

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0003 | `WriteHWRegRepeat` | nullptr | ⚠️ Medium | GPU command list — some games use this for bulk register writes |
| 0x0006 | `SetCommandList` | nullptr | 🔴 **High** | Used by some games to submit GPU command buffers. Causes GPU hang if called. |
| 0x0007 | `RequestDma` | nullptr | ⚠️ Medium | DMA transfer between DSP and GPU memory |
| 0x000A | `RegisterInterruptEvents` | nullptr | 🔴 **High** | Registers VBlank/etc interrupt events. Home Menu uses this. **May cause black screen on some titles.** |
| 0x000D | `SetDisplayTransfer` | nullptr | 🔴 **High** | LCD display transfer (framebuffer copy). **Critical for rendering — games using this path will show black screen.** |
| 0x000E | `SetTextureCopy` | nullptr | 🔴 **High** | Texture copy between VRAM and linear memory. **Used by many games for framebuffer upload.** |
| 0x000F | `SetMemoryFill` | nullptr | 🔴 **High** | GPU memory fill operation. **Used for clearing framebuffers.** |
| 0x001B | `ResetGpuCore` | nullptr | ⚠️ Medium | GPU reset — only called during GPU init |
| 0x001D | `SetTestCommand` | nullptr | 🟢 Low | Debug only |

**Impact**: Commands 0x000D, 0x000E, and 0x000F are the main paths for getting pixels to screen. The emulator currently routes rendering through Vulkan/OpenGL which handles this differently. However, titles that use the GSP command list path (rather than the register-based path) will fail to render.

---

### `DSP::DSP` (Audio)

**File**: `src/core/hle/service/dsp/dsp_dsp.cpp`

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0003 | `SendData` | nullptr | ⚠️ Medium | DSP data transfer — used for custom audio |
| 0x0004 | `SendDataIsEmpty` | nullptr | ⚠️ Medium | Checks if DSP send buffer is empty |
| 0x0005 | `SendFifoEx` | nullptr | ⚠️ Medium | Extended FIFO data send |
| 0x0006 | `RecvFifoEx` | nullptr | ⚠️ Medium | Extended FIFO data receive |
| 0x0008 | `GetSemaphore` | nullptr | ⚠️ Medium | DSP semaphore operations |
| 0x0009 | `ClearSemaphore` | nullptr | ⚠️ Medium | |
| 0x000A | `MaskSemaphore` | nullptr | ⚠️ Medium | |
| 0x000B | `CheckSemaphoreRequest` | nullptr | ⚠️ Medium | |
| 0x0018 | `GetPhysicalAddress` | nullptr | ⚠️ Medium | |
| 0x0019 | `GetVirtualAddress` | nullptr | ⚠️ Medium | |
| 0x001A–0x001E | IIR Filter / SPI | nullptr | 🟢 Low | Hardware-specific |
| 0x0021 | `GetIsDspOccupied` | nullptr | ⚠️ Medium | Checks DSP ownership — used before audio init |

**Impact**: The semaphore and FIFO commands are used by the DSP firmware module for audio pipe communication. If HLE DSP is used (default), these shouldn't be called. If LLE DSP is enabled, these **will crash**.

---

### `HID` (Human Interface Device)

**File**: `src/core/hle/service/hid/hid_user.cpp`, `hid_spvr.cpp`

All 4 nullptr handlers are internal NWM module commands. User-facing HID (GetKeys, etc.) is fully implemented. **No crash risk.**

---

### `NWM::INF` (Infrastructure WiFi)

**File**: `src/core/hle/service/nwm/nwm_inf.cpp`

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0001–0x0005 | Various | ✅ Implemented | — | |
| 0x0006 | `RecvBeaconBroadcastData` | nullptr | ⚠️ Medium | WiFi scanning — Home Menu uses this for WiFi indicator |
| 0x0007 | `ConnectToEncryptedAP` | nullptr | ⚠️ Medium | Connects to encrypted WiFi AP |
| 0x0008 | `ConnectToAP` | nullptr | ⚠️ Medium | Connects to open WiFi AP |
| 0x0009 | `Unknown9` | ✅ Implemented | — | Returns event handle |
| 0x000A | `UnknownA` | ✅ Implemented | — | |
| 0x000B | `UnknownB` | ✅ Implemented | — | Returns event handle |
| 0x000C–0x0010 | Various | ✅ Implemented | — | |

**Impact**: WiFi-related. The Home Menu WiFi indicator may show "disconnected" but this shouldn't cause crashes.

---

### `NWM::EXT` (Wireless Control)

**File**: `src/core/hle/service/nwm/nwm_ext.cpp`

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0004 | `SetWifiLinkLevel` | ✅ Works | — | Sets WiFi signal strength display |
| 0x0008 | `ControlWirelessEnabled` | nullptr | 🔴 **High** | **Enables/disables WiFi radio. Called by Home Menu settings.** If called and returns error, the NWM module may loop trying to toggle WiFi. |

**Impact**: `ControlWirelessEnabled` is called when the user toggles WiFi in System Settings. If it's nullptr and returns an error, the calling code may retry indefinitely, causing a **freeze**.

---

## 3. High Priority: Networking Services

These are critical for online features, Pretendo/Nimbus, and games with network functionality.

### `ssl:C` (SSL/TLS)

**File**: `src/core/hle/service/ssl/ssl_c.cpp`

**Most SSL commands are nullptr.** Only `Initialize` (0x0001) and `GenerateRandomData` (0x0011) are implemented.

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0001 | `Initialize` | ✅ Works | — | |
| 0x0002 | `CreateContext` | nullptr | 🔴 **High** | Creates SSL context for connection. **Every HTTPS game feature calls this.** |
| 0x0003 | `CreateRootCertChain` | nullptr | 🔴 **High** | Root certificate chain setup. **Required for certificate validation.** |
| 0x0004 | `DestroyRootCertChain` | ⚠️ Stubbed | — | Returns success, no cleanup |
| 0x0005 | `AddTrustedRootCA` | nullptr | 🔴 **High** | Adds CA certificate. **Required for HTTPS.** |
| 0x0006 | `RootCertChainAddDefaultCert` | nullptr | 🔴 **High** | Adds Nintendo's default root CA |
| 0x0007 | `RootCertChainRemoveCert` | nullptr | ⚠️ Medium | Certificate removal |
| 0x000D | `OpenClientCertContext` | nullptr | 🔴 **High** | Opens client certificate for mutual TLS |
| 0x000E | `OpenDefaultClientCertContext` | nullptr | 🔴 **High** | Opens default client cert (eShop) |
| 0x000F | `CloseClientCertContext` | nullptr | ⚠️ Medium | |
| 0x0011 | `GenerateRandomData` | ✅ Works | — | |
| 0x0012 | `InitializeConnectionSession` | nullptr | 🔴 **High** | Initializes SSL session. **Required for any SSL connection.** |
| 0x0013 | `StartConnection` | nullptr | 🔴 **High** | Starts SSL handshake. **Required for HTTPS.** |
| 0x0014 | `StartConnectionGetOut` | nullptr | 🔴 **High** | Non-blocking SSL connect |
| 0x0015 | `Read` | nullptr | 🔴 **High** | Reads encrypted data |
| 0x0016 | `ReadPeek` | nullptr | ⚠️ Medium | Peek at encrypted data |
| 0x0017 | `Write` | nullptr | 🔴 **High** | Writes encrypted data |
| 0x0018 | `ContextSetRootCertChain` | nullptr | 🔴 **High** | Associates cert chain with context |
| 0x0019 | `ContextSetClientCert` | nullptr | ⚠️ Medium | |
| 0x001B | `ContextClearOpt` | nullptr | ⚠️ Medium | |
| 0x001C | `ContextGetProtocolCipher` | nullptr | ⚠️ Medium | |
| 0x001E | `DestroyContext` | nullptr | ⚠️ Medium | |
| 0x001F | `ContextInitSharedmem` | nullptr | 🔴 **High** | Shared memory for SSL session data |

**Impact**: SSL is essentially **completely unimplemented**. Any game or service that uses HTTPS (eShop, SpotPass, online leaderboards, DLC checks, etc.) will fail. For Pretendo/Nimbus, SSL is **mandatory** — all Nintendo Network traffic is encrypted.

---

### `http:C` (HTTP)

**File**: `src/core/hle/service/http/http_c.cpp`

Most commands are implemented. The nullptr entries are for auxiliary features:

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0007 | `GetRequestError` | nullptr | ⚠️ Medium | Error reporting after failed requests |
| 0x000D | `SetProxy` | nullptr | ⚠️ Medium | HTTP proxy support |
| 0x000F | `SetBasicAuthorization` | nullptr | ⚠️ Medium | HTTP basic auth |
| 0x0010 | `SetSocketBufferSize` | nullptr | 🟢 Low | Socket tuning |
| 0x0027 | `SetClientCert` | nullptr | ⚠️ Medium | Client certificate for HTTPS |
| 0x002C | `SetSSLClearOpt` | nullptr | ⚠️ Medium | SSL options |
| 0x0035 | `SetDefaultProxy` | nullptr | ⚠️ Medium | |
| 0x0036 | `ClearDNSCache` | nullptr | 🟢 Low | |

**Impact**: Basic HTTP works. The missing `GetRequestError` means failed requests return opaque errors instead of specific error codes. For most games this is fine.

---

### `soc:U` (BSD Socket)

**File**: `src/core/hle/service/soc/soc_u.cpp`

The core socket functions (`Socket`, `Bind`, `Listen`, `Accept`, `Connect`, `Send`, `Recv`, `Close`, etc.) are all implemented. The nullptr entries are for ICMP:

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x001B | `ICMPSocket` | nullptr | 🟢 Low | Raw ICMP sockets — rarely used |
| 0x001C | `ICMPPing` | nullptr | 🟢 Low | Ping — not used by most games |
| 0x001D | `ICMPCancel` | nullptr | 🟢 Low | |
| 0x001E | `ICMPClose` | nullptr | 🟢 Low | |
| 0x001F | `GetResolverInfo` | nullptr | 🟢 Low | DNS resolver info |

**Impact**: Core networking works. Only ICMP (ping) is missing. DNS resolution goes through the host system.

---

## 4. Medium Priority: Game Support Services

These are used by specific games or features but aren't needed for basic operation.

### `AM` (Application Manager)

**Files**: `am.cpp`, `am_u.cpp`, `am_net.cpp`, `am_sys.cpp`, `am_app.cpp`

AM is heavily stubbed but the core operations (`GetTitleList`, `InstallTitle`, `DeleteTitle`) work. The nullptr entries are mostly internal/title-management commands.

| CMD | Name | Status | Notes |
|-----|------|--------|-------|
| 0x0001–0x0014 | Core operations | ✅ Implemented | Title management works |
| 0x0015+ | Extended operations | nullptr | Legacy/uncommon commands |

---

### `PTM` (Power/Time/Mode)

**Files**: `ptm_sysm.cpp`, `ptm_u.cpp`, `ptm_gets.cpp`, `ptm_play.cpp`

| CMD | Name | Status | Crash Risk | Notes |
|-----|------|--------|------------|-------|
| 0x0001 | `RegisterAlarmClient` | nullptr | ⚠️ Medium | Alarm/timer registration — some games use this for timed events |
| 0x0002 | `SetRtcAlarm` | nullptr | ⚠️ Medium | Sets real-time clock alarm |
| 0x0003 | `GetRtcAlarm` | nullptr | ⚠️ Medium | |
| 0x0004 | `CancelRtcAlarm` | nullptr | 🟢 Low | |
| 0x0005–0x000C | Battery/Steps | ✅ Implemented | |
| 0x000D | `SetPedometerRecordingMode` | nullptr | 🟢 Low | Step counter mode |
| 0x000E | `GetPedometerRecordingMode` | nullptr | 🟢 Low | |
| 0x000F | `GetStepHistoryAll` | nullptr | 🟢 Low | |
| 0x0401 | `SetRtcAlarmEx` | nullptr | ⚠️ Medium | Extended alarm |
| 0x0402 | `ReplySleepQuery` | nullptr | ⚠️ Medium | Sleep mode reply |
| 0x0403 | `NotifySleepPreparationComplete` | nullptr | ⚠️ Medium | |
| 0x0404 | `SetWakeupTrigger` | nullptr | ⚠️ Medium | Wake from sleep |
| 0x0405 | `GetAwakeReason` | nullptr | ⚠️ Medium | |

---

### `FRD` (Friend List)

**Files**: `frd_a.cpp`, `frd_u.cpp`, `frd.cpp`

Heavily stubbed but friend list operations (`GetFriendList`, `GetFriendKeyList`) are implemented. 35+ nullptr handlers for extended friend functionality. **No crash risk** — friend features degrade gracefully.

---

### `BOSS` (SpotPass)

**File**: `boss.cpp` (50 STUBBED calls)

All BOSS functions are implemented as stubs that return success with empty data. **No crash risk** but SpotPass content will never appear.

---

### `NIM` (Network Install Manager)

**Files**: `nim_u.cpp` (46 STUBBED), `nim_aoc.cpp`, `nim_s.cpp`

All NIM functions are stubbed. **No crash risk** but eShop downloads, DLC installation, and system updates won't work.

---

### `CECD` (StreetPass)

**File**: `cecd.cpp` (6 STUBBED)

Core StreetPass operations are stubbed. **No crash risk** — StreetPass data will be empty.

---

### `NFC`

**File**: `nfc_u.cpp`

| CMD | Name | Status | Notes |
|-----|------|--------|-------|
| 0x0001–0x000A | Core NFC | ✅ Implemented | Amiibo scanning works |
| 0x000B+ | Extended | nullptr | Tag writing, etc. |

---

### `CSND` (Circular Sound)

**File**: `csnd_snd.cpp` (11 STUBBED)

CSND is an older sound API. All functions are stubbed. Games using CSND for audio will be **silent** but won't crash. Most modern games use DSP instead.

---

### `IR` (Infrared)

**Files**: `ir_u.cpp` (18 nullptr), `ir_user.cpp` (17 nullptr)

Almost entirely unimplemented. Used by IR camera, infrared carts. **No crash risk** for normal games.

---

### `CAM` (Camera)

**Files**: `cam_u.cpp` (26 nullptr), `cam_s.cpp` (26 nullptr), `cam_c.cpp` (26 nullptr)

Camera commands are largely unimplemented. The actual camera capture (`TakePicture`, `Activate`, `SetReceiving)`) works but auxiliary commands don't. **No crash risk**.

---

## 5. Low Priority: Rarely Called Stubs

### `cfg:U` / `cfg:S` (Configuration)

| CMD | Name | Status | Notes |
|-----|------|--------|-------|
| 0x0001–0x0006 | Core config | ✅ Implemented | All essential config reading works |
| 0x0007 | `WriteToFirstByteCfgSavegame` | nullptr | Write config — not needed for emulation |
| 0x000B | `IsFangateSupported` | nullptr | TWL gate — not applicable |

**All essential configuration works.** Language, region, country, username, MAC address, etc. are all readable.

---

### `cfg:NOR` (NOR Flash Config)

4 nullptr handlers — NOR flash config is specific to development units. **No impact on retail games.**

---

### `DLP` (Download Play)

**Files**: `dlp_clnt.cpp` (3 nullptr), `dlp_base.cpp` (1 nullptr)

Download Play (game sharing) is partially implemented. The nullptr entries are for server-side operations. **No crash risk** but Download Play won't work between emulators.

---

### `LDR:RO` (Runtime Loader)

2 nullptr handlers for dynamic code loading. Homebrew that uses `ldr:ro` to load plugins will fail. **No impact on normal games.**

---

### `News`

1 nullptr handler. News content delivery is stubbed. **No impact.**

---

### `QTM` (Face Tracking)

1 nullptr handler. Face tracking (New 3DS only) is not emulated. **No impact.**

---

### `MVD` (Video Decoder)

12 nullptr handlers. Hardware video decoding is not emulated. Games that use MVD for video playback will fail to play videos. **No crash risk** — videos are skipped.

---

### `MCU::HWC` (MCU Hardware Control)

15 nullptr handlers. MCU communication ( LEDs, RTC, etc.) is mostly stubbed. The basic LED control works. **No crash risk**.

---

### `PS` (Process Selection)

13 nullptr handlers. Process services are internal. **No crash risk.**

---

## 6. SVC Table Unimplemented Entries

These are Supervisor Call (SVC) entries that return `ResultInvalidCombination` when called:

| SVC | Name | Risk | Notes |
|-----|------|------|-------|
| 0x04 | `GetProcessAffinityMask` | ⚠️ Medium | Some multithreaded titles call this |
| 0x05 | `SetProcessAffinityMask` | ⚠️ Medium | Same |
| 0x06 | `GetProcessIdealProcessor` | ⚠️ Medium | |
| 0x07 | `SetProcessIdealProcessor` | ⚠️ Medium | |
| 0x0D | `GetThreadAffinityMask` | ⚠️ Medium | |
| 0x0E | `SetThreadAffinityMask` | ⚠️ Medium | |
| 0x0F | `GetThreadIdealProcessor` | ⚠️ Medium | |
| 0x10 | `SetThreadIdealProcessor` | ⚠️ Medium | |
| 0x11 | `GetCurrentProcessorNumber` | ⚠️ Medium | Used by multicore titles |
| 0x12 | `Run` | 🔴 **High** | Creates and runs a new process — used by some system titles |
| 0x26 | `SignalAndWait` | ⚠️ Medium | Signals a mutex/event and waits atomically |
| 0x2E–0x31 | `SendSyncRequest1–4` | 🔴 **High** | Batched IPC — used by DSP module |
| 0x3B | `GetThreadContext` | ⚠️ Medium | Thread context inspection |
| 0x3E | `ControlPerformanceCounter` | 🟢 Low | Performance monitoring |

**Impact**: SVCs 0x12 (`Run`) and 0x2E–0x31 (`SendSyncRequest1–4`) are the most dangerous. `Run` is used by the loader to spawn system processes. `SendSyncRequest1–4` are used by the DSP module for batched IPC.

---

## 7. Per-Service Detailed Breakdown

### NWM (Nintendo WiFi Module) — Complete Status

| Service | Implemented | Unimplemented | Crash Risk |
|---------|-------------|---------------|------------|
| `nwm::SOC` | 2 (GetMACAddress, GetMbufPoolInformation) | 11 | ✅ Fixed |
| `nwm::UDS` | 35+ (full local wireless) | 1 (Scrap) | 🟢 Low |
| `nwm::INF` | 8 | 3 (RecvBeacon, ConnectAP) | ⚠️ Medium |
| `nwm::EXT` | 5 | 1 (ControlWireless) | 🔴 High |
| `nwm::CEC` | 1 | 1 (SendProbeRequest) | 🟢 Low |
| `nwm::SAP` | Full | 0 | ✅ Complete |
| `nwm::TST` | Full | 0 | ✅ Complete |

### FS (Filesystem) — Complete Status

67 nullptr handlers, but **all commonly used FS operations are implemented**:
- ✅ OpenFile, CloseFile, ReadFile, WriteFile
- ✅ CreateFile, DeleteFile, RenameFile
- ✅ CreateDirectory, DeleteDirectory
- ✅ OpenArchive, CloseArchive
- ✅ GetFreeBytes, GetTotalBytes
- ✅ GetSpecialContentIndex
- ✅ FormatSaveData, CreateSystemSaveData

The nullptr entries are for:
- Low-level NAND/SDMC operations (`GetSdmcCid`, `GetNandCid`, etc.)
- Card slot power management
- Debug/logging functions
- `DeleteSystemSaveData` (only used during system initialization)

### CFG (Configuration) — Complete Status

All essential configuration reading is implemented:
- ✅ `GetSystemModel`, `GetCountryCode`, `GetLanguage`
- ✅ `GetMacAddress`, `GetAggregationBits`
- ✅ `GetConfigInfoBlk2`, `SetConfigInfoBlk2`
- ✅ `GetTransferableId`, `IsSoundRecEnabled`
- ✅ `GetParentalControls`, `GetAgreeWithDeviceAgreements`

Missing: `WriteToFirstByteCfgSavegame`, `IsFangateSupported` — neither is needed.

---

## Quick Reference: What to Implement Next

### Tier 1 — Fixes Known Crashes
1. ~~`nwm::SOC` CMD 0x09 — **DONE**~~
2. `NWM::EXT` CMD 0x08 — `ControlWirelessEnabled` — Prevent WiFi toggle freeze
3. SVC 0x2E–0x31 — `SendSyncRequest1–4` — Batched IPC for DSP module
4. `GSP::GPU` CMD 0x000A — `RegisterInterruptEvents` — VBlank interrupt registration

### Tier 2 — Fixes Home Menu Completeness
5. `SRV` CMD 0x04 — `UnregisterService` — Proper service cleanup
6. `PTM` CMD 0x0001 — `RegisterAlarmClient` — Alarm/timer support
7. `NWM::INF` CMD 0x0006 — `RecvBeaconBroadcastData` — WiFi scanning

### Tier 3 — Enables Online Features (Pretendo/Nimbus)
8. **Entire `ssl:C` service** — All SSL commands
9. `http:C` CMD 0x0007 — `GetRequestError`
10. `http:C` CMD 0x0027 — `SetClientCert`

### Tier 4 — Nice to Have
11. `GSP::GPU` CMDs 0x0D/0x0E/0x0F — Display transfer commands
12. `DSP::DSP` semaphore commands — LLE DSP support
13. `PTM` alarm commands — Sleep/wake support

---

*Last updated: 2026-08-23*
*Generated by analysis of `src/core/hle/service/` source code*
