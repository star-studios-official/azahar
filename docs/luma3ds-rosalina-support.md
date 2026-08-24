# Luma3DS / Rosalina Support — Implementation Guide

> **Purpose**: Document what Luma3DS and Rosalina features can be implemented in the Azahar emulator, based on analysis of `ref/Luma3DS` source code. This covers the plugin loader, custom SVCs, error display, input redirection, and other CFW-specific functionality.

---

## Table of Contents

1. [Luma3DS Architecture Overview](#1-luma3ds-architecture-overview)
2. [What's Already Implemented](#2-whats-already-implemented)
3. [Implementable: Plugin Loader (plg:ldr)](#3-implementable-plugin-loader-plgldr)
4. [Implementable: Custom SVCs](#4-implementable-custom-svcs)
5. [Implementable: Error Display (err:d)](#5-implementable-error-display-errd)
6. [Implementable: Input Redirection](#6-implementable-input-redirection)
7. [Implementable: NTP Time Sync](#7-implementable-ntp-time-sync)
8. [Implementable: Miscellaneous Features](#8-implementable-miscellaneous-features)
9. [Not Implementable (Hardware-Dependent)](#9-not-implementable-hardware-dependent)
10. [Implementation Priority Matrix](#10-implementation-priority-matrix)

---

## 1. Luma3DS Architecture Overview

Luma3DS is a CFW (Custom Firmware) that runs as multiple sysmodules:

### Sysmodules

| Module | Purpose | Emulatable? |
|--------|---------|-------------|
| **rosalina** | Menu, debugger, plugin loader, error display | ✅ Partially |
| **loader** | Custom title loader with patches | ⚠️ Partially |
| **pm** | Process manager with memory limits | ✅ Via existing PM |
| **pxi** | PxiFS (game card export/import) | ✅ Already stubbed |
| **sm** | Service manager (replaces native) | ✅ Already implemented |

### Rosalina Components

From `ref/Luma3DS/sysmodules/rosalina/source/`:

| Component | File | Purpose |
|-----------|------|---------|
| Plugin Loader | `plugin/plgldr.c`, `plgloader.c` | Loads 3GX plugins into games |
| Error Display | `errdisp.c` | Shows system error screens |
| Input Redirection | `input_redirection.c` | Redirects HID input over network |
| NTP Sync | `ntp.c` | Network time synchronization |
| GDB Server | `gdb/*.c` | Remote debugging |
| Screen Filters | `menus/screen_filters.c` | Display color correction |
| Cheats | `menus/cheats.c` | Cheat code execution |
| Process List | `menus/process_list.c` | Process management UI |
| Memory Block | `plugin/memoryblock.c` | Plugin memory management |
| Shell | `shell.c` | Shell open/close handling |
| Sleep | `sleep.c` | Sleep/wake management |

---

## 2. What's Already Implemented

### ✅ SVC 0xB0 — `ControlService` (Luma3DS Custom SVC)

**Status**: Fully implemented in `src/core/hle/kernel/svc.cpp`

From `ref/Luma3DS/k11_extension/source/svc/ControlService.c`:

```c
typedef enum ServiceOp {
    SERVICEOP_STEAL_CLIENT_SESSION = 0, // Steal a client session by service name
    SERVICEOP_GET_NAME,                 // Get service name from handle
} ServiceOp;
```

**Implementation**: Already done. Supports both operations:
- `STEAL_CLIENT_SESSION` (op=0): Connects to a service and returns the handle
- `GET_NAME` (op=1): Returns the service name for a given handle

**Used by**: PKSM, Checkpoint, and other homebrew that steal service sessions.

### ✅ `hb:ldr` (Homebrew Loader)

**Status**: Implemented as a stub in `src/core/hle/service/hbloader/hbldr.cpp`

Returns the homebrew loader named port for Luma3DS compatibility.

### ✅ `plgldr` Service Registration

**Status**: The `plg:ldr` service name is registered in the emulator's service table.

---

## 3. Implementable: Plugin Loader (plg:ldr)

### What It Does

From `ref/Luma3DS/sysmodules/rosalina/source/plugin/plgloader.c`:

The plugin loader handles 3GX plugin files — native ARM code that gets injected into running games. It provides:

1. **Loading plugins** into a target process
2. **Memory management** (swap mode, MODE3, extended memory)
3. **Plugin configuration** (persistent parameters)
4. **Display** (menus, messages, error screens)
5. **Swap** (on O3DS, plugins are swapped to/from SD when Home Menu is entered)

### IPC Commands

From `ref/Luma3DS/sysmodules/rosalina/source/plugin/plgloader.c`:

| CMD | Name | Description | Emulatable? |
|-----|------|-------------|-------------|
| 1 | Load plugin | Loads 3GX into target process | ⚠️ Complex |
| 2 | IsEnabled | Check if plugin loader is enabled | ✅ Easy |
| 3 | SetState | Enable/disable plugin loader | ✅ Easy |
| 4 | SetLoadParameters | Set plugin path, config, memory strategy | ✅ Easy |
| 5 | DisplayMenu | Show plugin menu to user | ❌ No UI |
| 6 | DisplayMessage | Show message dialog | ❌ No UI |
| 7 | DisplayErrorMessage | Show error with error code | ❌ No UI |
| 8 | GetVersion | Return PLGLDR_VERSION | ✅ Easy |
| 9 | GetArbiter | Return address arbiter handle | ✅ Easy |
| 10 | GetPluginPath | Return current plugin path | ✅ Easy |
| 11 | SetRosalinaMenuBlock | Block Rosalina menu opening | ✅ Easy |
| 12 | SetSwapSettings | Configure plugin swap functions | ⚠️ Complex |
| 13 | SetExeLoadSettings | Configure plugin exe load functions | ⚠️ Complex |
| 14 | ClearUserLoadParameters | Reset user load parameters | ✅ Easy |

### Implementation Strategy

**Phase 1 — Stub the service** (prevents crashes):

```cpp
// src/core/hle/service/plgldr/plgldr.cpp
void PLGLDR::HandleCommands(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 cmd_id = ctx.CommandID();
    
    switch (cmd_id) {
    case 2: { // IsEnabled
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess);
        rb.Push<u32>(0); // Always disabled in emulation
        break;
    }
    case 3: { // SetState
        const u32 enabled = rp.Pop<u32>();
        LOG_INFO(Service_PLGLDR, "SetState: enabled={}", enabled);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultSuccess);
        break;
    }
    case 8: { // GetVersion
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess);
        rb.Push<u32>(0x01000002); // PLGLDR_VERSION
        break;
    }
    case 9: { // GetArbiter
        // Create an address arbiter for plugin synchronization
        auto arbiter = system.Kernel().CreateAddressArbiter("plg:ldr arbiter");
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 1);
        rb.Push(ResultSuccess);
        rb.PushCopyObjects(arbiter);
        break;
    }
    case 10: { // GetPluginPath
        std::string path = "/luma/plugins/plugin.3gx";
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(ResultSuccess);
        rb.PushStaticBuffer(std::vector<u8>(path.begin(), path.end()), 0);
        break;
    }
    default:
        LOG_WARNING(Service_PLGLDR, "Unimplemented command {}", cmd_id);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultSuccess);
        break;
    }
}
```

**Phase 2 — Full plugin loading** (requires ARM code injection):

This is extremely complex and requires:
1. Loading the 3GX file format (from `ref/Luma3DS/sysmodules/rosalina/source/plugin/3gx.c`)
2. Mapping plugin code into the target process
3. Setting up the plugin header at `0x07000000`
4. Starting plugin threads
5. Handling plugin events (PLG_OK, PLG_HOME_ENTER, etc.)

**Recommendation**: Phase 1 only. 3GX plugins are native ARM code that require JIT/native execution — they cannot run in an interpreter. Stub the service to prevent crashes.

---

## 4. Implementable: Custom SVCs

### Luma3DS Custom SVCs

From `ref/Luma3DS/k11_extension/source/svc/`:

| SVC | Name | Status | Notes |
|-----|------|--------|-------|
| 0xB0 | ControlService | ✅ Implemented | Service session stealing |
| 0xB1 | CopyHandle | ❌ Missing | Copy handle to another process |
| 0xB2 | TranslateHandle | ❌ Missing | Translate handle between processes |
| 0xB3 | ControlProcess | ✅ Implemented | Process control (schedule threads, etc.) |
| 0xB4 | CopyHandle (alt) | ❌ Missing | Alternative copy handle |
| 0xB5 | TranslateHandle (alt) | ❌ Missing | Alternative translate handle |
| 0xB6 | SetGpuProt | ❌ Missing | Set GPU protection (for plugin memory) |
| 0xB7 | MapProcessMemoryEx | ❌ Missing | Map process memory with flags |
| 0xB8 | UnmapProcessMemoryEx | ❌ Missing | Unmap process memory |
| 0xB9 | SetWifiEnabled | ❌ Missing | Enable/disable WiFi |
| 0xBA | CustomBackdoor | ❌ Missing | Execute custom kernel backdoor |
| 0xBB | ControlService (extended) | ❌ Missing | Extended service control |

### Implementation Priority

**High Priority** (used by homebrew):

1. **SVC 0xB1 — CopyHandle**: Copies a handle from one process to another
   - Used by: Plugin loader, debugging tools
   - Implementation: Duplicate handle in target process's handle table

2. **SVC 0xB2 — TranslateHandle**: Translates a handle through session translation
   - Used by: Service manager extensions
   - Implementation: Look up translated handle in session info

3. **SVC 0xB9 — SetWifiEnabled**: Enable/disable WiFi radio
   - Used by: WiFi toggle in Rosalina menu
   - Implementation: Toggle internal WiFi state flag

**Medium Priority**:

4. **SVC 0xB7 — MapProcessMemoryEx**: Map memory with extended flags
   - Used by: Plugin loader for memory strategies
   - Implementation: Map with PRIVATE/SHARED flags

5. **SVC 0xB8 — UnmapProcessMemoryEx**: Unmap previously mapped memory
   - Used by: Plugin cleanup
   - Implementation: Unmap the mapped region

**Low Priority** (kernel-only, rarely used):

6. **SVC 0xBA — CustomBackdoor**: Execute arbitrary kernel code
   - Only used by: Debugging tools, very advanced homebrew
   - Implementation: Not recommended — security risk

---

## 5. Implementable: Error Display (err:d)

### What It Does

From `ref/Luma3DS/sysmodules/rosalina/source/errdisp.c`:

When a fatal error occurs, the 3DS shows a red error screen. Rosalina intercepts these errors and displays them in a user-friendly format.

### How It Works

1. Rosalina subscribes to error notifications via `err:f` (Error Report service)
2. When an error occurs, it receives the error code and context
3. It displays the error information on screen
4. It can optionally log the error to SD card

### Implementation Strategy

For the emulator, we can:

1. **HLE the `err:f` service** to capture error notifications
2. **Display errors in the log** (since we don't have a red screen)
3. **Optionally show an iOS alert** for fatal errors

```cpp
// Stub implementation
void ERR_F::SetUserError(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 error_code = rp.Pop<u32>();
    LOG_CRITICAL(Service_ERR, "User error: 0x{:08X}", error_code);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

---

## 6. Implementable: Input Redirection

### What It Does

From `ref/Luma3DS/sysmodules/rosalina/source/input_redirection.c`:

Rosalina can redirect HID (input) data over the network, allowing remote control of the 3DS from a PC.

### Implementation Strategy

For the emulator, this is **already handled** by the existing HID service. The emulator's HID input comes from:
- Touch screen (iOS touch events)
- Buttons (on-screen controls or gamepad)
- Circle pad (analog stick)
- Gyroscope/accelerometer (if available)

No additional implementation needed — the emulator's HID already provides this functionality.

---

## 7. Implementable: NTP Time Sync

### What It Does

From `ref/Luma3DS/sysmodules/rosalina/source/ntp.c`:

Synchronizes the 3DS clock with an NTP server over the network.

### Implementation Strategy

For the emulator, we can:

1. **Use the host system clock** (already done — the emulator uses `std::time`)
2. **Optionally sync with NTP** if the user wants accurate time
3. **Store the NTP offset** in the system config

```cpp
// In PTM service
void PTM::GetRtcTime(Kernel::HLERequestContext& ctx) {
    // Return current time from host system
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    
    // Convert to 3DS RTC format
    u32 year = tm->tm_year + 1900;
    u32 month = tm->tm_mon + 1;
    u32 day = tm->tm_mday;
    u32 hour = tm->tm_hour;
    u32 minute = tm->tm_min;
    u32 second = tm->tm_sec;
    
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push<u32>((year << 16) | (month << 8) | day);
    rb.Push<u32>((hour << 16) | (minute << 8) | second);
}
```

---

## 8. Implementable: Miscellaneous Features

### 8.1 Shell Open/Close Handling

From `ref/Luma3DS/sysmodules/rosalina/source/shell.c`:

Rosalina handles shell open/close events to show/hide the menu.

**Implementation**: The emulator doesn't have a physical shell, but we can:
- Map shell open → app resume
- Map shell close → app pause
- Or simply always report shell as open

### 8.2 Sleep/Wake Management

From `ref/Luma3DS/sysmodules/rosalina/source/sleep.c`:

Rosalina handles sleep/wake transitions, saving and restoring state.

**Implementation**: The emulator doesn't sleep, but we need to:
- Handle `PTMNOTIFID_SLEEP_REQUESTED` → Return success (allow sleep)
- Handle `PTMNOTIFID_GOING_TO_SLEEP` → Save emulator state
- Handle `PTMNOTIFID_FULLY_AWAKE` → Restore emulator state

### 8.3 Screen Filters

From `ref/Luma3DS/sysmodules/rosalina/source/menus/screen_filters.c`:

Rosalina provides display color correction (night mode, color temperature).

**Implementation**: Can be done in the Vulkan/OpenGL renderer as post-processing effects.

### 8.4 Cheats

From `ref/Luma3DS/sysmodules/rosalina/source/menus/cheats.c`:

Rosalina executes Gateway-style cheat codes.

**Implementation**: The emulator already has cheat support in `src/core/cheats/`. Luma3DS cheats use the same Gateway format.

### 8.5 Process List / Management

From `ref/Luma3DS/sysmodules/rosalina/source/menus/process_list.c`:

Rosalina shows a list of running processes and allows killing them.

**Implementation**: The emulator already has process management via the PM service.

---

## 9. Not Implementable (Hardware-Dependent)

These features are fundamentally tied to 3DS hardware and cannot be emulated:

| Feature | Reason |
|---------|--------|
| **GPU Protection (SetGpuProt)** | Hardware-specific GPU memory protection |
| **CustomBackdoor** | Arbitrary kernel code execution |
| **Address Arbiter (advanced)** | Kernel-level synchronization primitives |
| **Plugin Memory Swap** | Physical memory swapping to SD card |
| **Display Capture** | Direct framebuffer access |
| **LED Control** | Hardware LED GPIO |
| **RTC (Real-Time Clock)** | Hardware RTC chip |
| **WiFi Radio Control** | Hardware WiFi SDIO |
| **NFC** | Hardware NFC reader |
| **IR** | Hardware infrared port |

---

## 10. Implementation Priority Matrix

### Tier 1 — Prevents Crashes (Do First)

| Feature | Effort | Impact |
|---------|--------|--------|
| `plg:ldr` service stub | Low | Prevents plugin loader crash |
| SVC 0xB1 (CopyHandle) | Low | Used by homebrew |
| SVC 0xB2 (TranslateHandle) | Low | Used by homebrew |
| SVC 0xB9 (SetWifiEnabled) | Low | WiFi toggle |

### Tier 2 — Improves Compatibility

| Feature | Effort | Impact |
|---------|--------|--------|
| `plg:ldr` full implementation | High | 3GX plugin support |
| SVC 0xB7 (MapProcessMemoryEx) | Medium | Plugin memory mapping |
| Error display (err:d) | Low | Better error reporting |
| NTP time sync | Low | Accurate clock |

### Tier 3 — Nice to Have

| Feature | Effort | Impact |
|---------|--------|--------|
| Screen filters | Medium | Display correction |
| Input redirection | Low | Already handled by emulator |
| Shell handling | Low | App pause/resume |
| Sleep/wake | Low | State management |

---

## Appendix A: Rosalina Service Registration

From `ref/Luma3DS/sysmodules/rosalina/source/main.c`:

```c
static const ServiceManagerServiceEntry services[] = {
    { "plg:ldr", 1, PluginLoader__HandleCommands, true },
    { NULL },
};
```

Rosalina registers only **one service**: `plg:ldr` (Plugin Loader). All other functionality is accessed through:
- **Custom SVCs** (0xB0–0xBB)
- **Kernel notifications** (sleep, shell, pre-termination)
- **Memory-mapped config** (shared memory at `0x1FF80000`)

## Appendix B: Rosalina Kernel Notifications

From `ref/Luma3DS/sysmodules/rosalina/source/main.c`:

```c
static const ServiceManagerNotificationEntry notifications[] = {
    { 0x100,                       handleTermNotification },        // Process termination
    { PTMNOTIFID_SLEEP_REQUESTED,  handleSleepNotification },      // Sleep request
    { PTMNOTIFID_SLEEP_DENIED,     handleSleepNotification },      // Sleep denied
    { PTMNOTIFID_SLEEP_ALLOWED,    handleSleepNotification },      // Sleep allowed
    { PTMNOTIFID_GOING_TO_SLEEP,   handleSleepNotification },      // Going to sleep
    { PTMNOTIFID_FULLY_WAKING_UP,  handleSleepNotification },      // Waking up
    { PTMNOTIFID_FULLY_AWAKE,      handleSleepNotification },      // Fully awake
    { PTMNOTIFID_HALF_AWAKE,       handleSleepNotification },      // Half awake
    { 0x213,                       handleShellNotification },      // Shell opened
    { 0x214,                       handleShellNotification },      // Shell closed
    { 0x1000,                      handleNextApplicationDebuggedByForce }, // Debug
    { 0x2000,                      handlePreTermNotification },    // Pre-termination
    { 0x1001,                      PluginLoader__HandleKernelEvent }, // Plugin event
    { 0x000, NULL },
};
```

## Appendix C: Key Files for Implementation

| File | Purpose | What to Reference |
|------|---------|-------------------|
| `ref/Luma3DS/sysmodules/rosalina/source/plugin/plgloader.c` | Plugin loader commands | CMD 1-14 handlers |
| `ref/Luma3DS/sysmodules/rosalina/source/plugin/plgldr.c` | Plugin loader client | IPC protocol |
| `ref/Luma3DS/sysmodules/rosalina/source/plugin/3gx.c` | 3GX file format | Plugin header structure |
| `ref/Luma3DS/sysmodules/rosalina/source/plugin/memoryblock.c` | Plugin memory | Memory strategies |
| `ref/Luma3DS/sysmodules/rosalina/source/errdisp.c` | Error display | Error notification handling |
| `ref/Luma3DS/sysmodules/rosalina/source/input_redirection.c` | Input redirect | HID over network |
| `ref/Luma3DS/sysmodules/rosalina/source/ntp.c` | NTP sync | Time synchronization |
| `ref/Luma3DS/k11_extension/source/svc/ControlService.c` | SVC 0xB0 | Service stealing |
| `ref/Luma3DS/k11_extension/source/svc/CopyHandle.c` | SVC 0xB1 | Handle copying |
| `ref/Luma3DS/k11_extension/source/svc/TranslateHandle.c` | SVC 0xB2 | Handle translation |
| `ref/Luma3DS/k11_extension/source/svc/SetWifiEnabled.c` | SVC 0xB9 | WiFi control |
| `ref/Luma3DS/k11_extension/source/svc/MapProcessMemoryEx.c` | SVC 0xB7 | Memory mapping |
| `ref/Luma3DS/k11_extension/source/svc/ControlProcess.c` | SVC 0xB3 | Process control |

---

*Last updated: 2026-08-23*
*Reference source: ref/Luma3DS*
