# Luma3DS Full Implementation Plan — 100% Compatibility

> **Goal**: Achieve complete Luma3DS/Rosalina compatibility in the Azahar emulator, enabling all CFW features, homebrew, plugins, and system modifications to work as they do on real hardware.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Component-by-Component Implementation](#2-component-by-component-implementation)
3. [Kernel Extension (k11_extension)](#3-kernel-extension-k11_extension)
4. [System Modules Replacement](#4-system-modules-replacement)
5. [Rosalina Overlay Menu](#5-rosalina-overlay-menu)
6. [Plugin System (3GX)](#6-plugin-system-3gx)
7. [Boot Process & FIRM Loading](#7-boot-process--firm-loading)
8. [Feature Parity Checklist](#8-feature-parity-checklist)
9. [Implementation Order](#9-implementation-order)
10. [Technical Deep Dives](#10-technical-deep-dives)

---

## 1. Architecture Overview

### Luma3DS Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Luma3DS Architecture                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   arm9boot   │  │   arm11boot   │  │  k11_ext     │      │
│  │  (loader)    │  │  (settings)   │  │  (kernel)    │      │
│  │  Patches FIRM│  │  Chainloader  │  │  Custom SVCs │      │
│  │  Injects KIPs│  │  Settings UI  │  │  IPC hooks   │      │
│  └──────┬───────┘  └──────┬────────┘  └──────┬───────┘      │
│         │                 │                   │              │
│         ▼                 ▼                   ▼              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Kernel11 (patched NATIVE_FIRM)          │    │
│  │  • Extended SVC table (0x80-0xB3)                    │    │
│  │  • IPC interception                                  │    │
│  │  • Memory protection hooks                           │    │
│  │  • Debug event system                                │    │
│  └─────────────────────┬───────────────────────────────┘    │
│                         │                                    │
│  ┌──────────────────────┴──────────────────────────────┐    │
│  │              System Modules (KIPs)                   │    │
│  │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────────────┐  │    │
│  │  │ sm  │ │ pm  │ │loader│ │pxi  │ │  rosalina   │  │    │
│  │  │     │ │     │ │     │ │     │ │  (custom)   │  │    │
│  │  └─────┘ └─────┘ └─────┘ └─────┘ └─────────────┘  │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              User Applications                       │    │
│  │  • Games • Homebrew (3DSX) • System Apps             │    │
│  │  • Plugins (3GX) • GDB debugging                     │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### What "100% Luma3DS" Means

| Feature | Description | Priority |
|---------|-------------|----------|
| Custom SVCs | 0x80-0xB3 kernel calls | 🔴 Critical |
| SM replacement | Service access control removal | 🔴 Critical |
| PM replacement | Process management extensions | 🔴 Critical |
| Loader replacement | Title patching, 3DSX loading | 🔴 Critical |
| Rosalina menu | In-game overlay | 🟡 High |
| Plugin system | 3GX loading | 🟡 High |
| GDB server | Remote debugging | 🟢 Medium |
| Screen filters | Display correction | 🟢 Medium |
| Input redirection | Network HID | 🟢 Medium |
| NTP sync | Time synchronization | 🟢 Medium |
| Error display | Fatal error screens | 🟢 Medium |
| LayeredFS | Asset redirection | 🟢 Medium |
| Locale emulation | Per-game language | 🟢 Medium |

---

## 2. Component-by-Component Implementation

### 2.1 Kernel Extension — Full SVC Table

From `ref/Luma3DS/k11_extension/source/svc.c`, Luma3DS hooks the following SVCs:

```c
void buildAlteredSvcTable(void)
{
    // Hooked official SVCs
    alteredSvcTable[0x01] = ControlMemoryHookWrapper;      // Memory control
    alteredSvcTable[0x03] = ExitProcessHookWrapper;         // Process exit
    alteredSvcTable[0x08] = CreateThreadHookWrapper;        // Thread creation (N3DS)
    alteredSvcTable[0x29] = GetHandleInfoHookWrapper;       // Handle info
    alteredSvcTable[0x2A] = GetSystemInfoHookWrapper;       // System info
    alteredSvcTable[0x2B] = GetProcessInfoHookWrapper;      // Process info
    alteredSvcTable[0x2C] = GetThreadInfoHookWrapper;       // Thread info
    alteredSvcTable[0x2D] = ConnectToPortHookWrapper;       // Port connection
    alteredSvcTable[0x32] = SendSyncRequestHook;            // IPC send
    alteredSvcTable[0x3C] = BreakHook;                      // Break/debug

    // Luma3DS-specific official SVCs (in unused slots)
    alteredSvcTable[0x59] = SetGpuProt;                     // GPU protection
    alteredSvcTable[0x5A] = SetWifiEnabled;                 // WiFi control
    alteredSvcTable[0x7B] = Backdoor;                       // Kernel backdoor
    alteredSvcTable[0x7C] = KernelSetStateHook;             // Kernel state

    // Custom SVCs (new slots)
    alteredSvcTable[0x80] = CustomBackdoor;                 // Custom backdoor
    alteredSvcTable[0x90] = convertVAToPA;                  // VA to PA
    alteredSvcTable[0x91] = flushDataCacheRange;            // Flush D-cache
    alteredSvcTable[0x92] = flushEntireDataCache;           // Flush all D-cache
    alteredSvcTable[0x93] = invalidateInstructionCacheRange;// Invalidate I-cache
    alteredSvcTable[0x94] = invalidateEntireInstructionCache;// Invalidate all I-cache
    alteredSvcTable[0xA0] = MapProcessMemoryExWrapper;      // Map process memory
    alteredSvcTable[0xA1] = UnmapProcessMemoryEx;           // Unmap process memory
    alteredSvcTable[0xA2] = ControlMemoryEx;                // Extended memory control
    alteredSvcTable[0xA3] = ControlMemoryUnsafeWrapper;     // Unsafe memory control

    // Service control SVCs
    alteredSvcTable[0xB0] = ControlService;                 // Service stealing
    alteredSvcTable[0xB1] = CopyHandleWrapper;              // Copy handle
    alteredSvcTable[0xB2] = TranslateHandleWrapper;         // Translate handle
    alteredSvcTable[0xB3] = ControlProcess;                 // Process control
}
```

**Emulator implementation**: Each SVC maps directly to an emulator function. See Section 3 for details.

### 2.2 System Module Replacements

#### SM (Service Manager)

From `ref/Luma3DS/sysmodules/sm/`:

Luma3DS's SM **removes service access control**, allowing any process to connect to any service. This is critical for homebrew.

**Emulator implementation**:
```
Current state: SM already allows unrestricted access
Required change: None — the emulator's SM doesn't enforce access control
```

#### PM (Process Manager)

From `ref/Luma3DS/sysmodules/pm/`:

Luma3DS's PM adds:
- Break-on-start GDB support
- FS access control lifting
- Process memory limit management

**Emulator implementation**:
```
Current state: PM is implemented but lacks Luma3DS extensions
Required changes:
  1. Add break-on-start notification support
  2. Add FS access control bypass
  3. Add memory limit configuration
```

#### Loader

From `ref/Luma3DS/sysmodules/loader/`:

The loader is the most complex component. It handles:
- **Title patching** (game mods, locale emulation, LayeredFS)
- **3DSX homebrew loading**
- **System module replacement** (loading custom CXIs)
- **Process memory management**

**Emulator implementation**:
```
Current state: Basic loader exists
Required changes:
  1. Add patchCode() function for title-specific patches
  2. Add 3DSX loader support
  3. Add LayeredFS redirection
  4. Add locale emulation
  5. Add system module replacement
```

#### Pxi

From `ref/Luma3DS/sysmodules/pxi/`:

Reimplemented for completeness. Handles Arm11↔Arm9 communication.

**Emulator implementation**:
```
Current state: PxiFS is stubbed
Required changes: Minimal — mostly internal
```

---

## 3. Kernel Extension (k11_extension)

### 3.1 Custom SVC Implementations

Each custom SVC needs to be implemented in the emulator's SVC handler (`src/core/hle/kernel/svc.cpp`).

#### SVC 0x80 — CustomBackdoor

**Purpose**: Execute arbitrary code in kernel context.

**From** `ref/Luma3DS/k11_extension/source/svc/CustomBackdoor.c`:
```c
Result CustomBackdoor(void *callback, ...)
{
    // Executes callback in kernel context
    // Used by: advanced homebrew, debugging tools
}
```

**Emulator implementation**:
```cpp
Result SVC::CustomBackdoor() {
    // For emulation, we can:
    // 1. Log the call and return success
    // 2. Execute a callback function if provided
    // 3. For security, limit what the callback can do
    
    LOG_WARNING(Kernel_SVC, "CustomBackdoor called (stubbed)");
    return ResultSuccess;
}
```

#### SVC 0x90 — ConvertVAToPA

**Purpose**: Convert virtual address to physical address.

**From** `ref/Luma3DS/k11_extension/source/svc.c`:
```c
alteredSvcTable[0x90] = convertVAToPA;
```

**Emulator implementation**:
```cpp
Result SVC::ConvertVAToPA() {
    const u32 va = GetReg(1);
    // Use the memory system to translate
    auto& memory = system.Memory();
    const u32 pa = memory.GetPhysicalAddress(va);
    SetReg(1, pa);
    return ResultSuccess;
}
```

#### SVC 0x91-0x94 — Cache Operations

**Purpose**: Data/instruction cache management.

**Emulator implementation**:
```cpp
Result SVC::FlushDataCacheRange() {
    const u32 addr = GetReg(1);
    const u32 size = GetReg(2);
    // In emulation, caches are managed by the host CPU
    // Just return success
    return ResultSuccess;
}

Result SVC::FlushEntireDataCache() {
    // Same as above — host handles this
    return ResultSuccess;
}

Result SVC::InvalidateInstructionCacheRange() {
    const u32 addr = GetReg(1);
    const u32 size = GetReg(2);
    // Invalidate the JIT code cache for this range
    system.InvalidateJITCache(addr, size);
    return ResultSuccess;
}

Result SVC::InvalidateEntireInstructionCache() {
    system.InvalidateEntireJITCache();
    return ResultSuccess;
}
```

#### SVC 0xA0 — MapProcessMemoryEx

**Purpose**: Map memory from one process to another with extended flags.

**From** `ref/Luma3DS/k11_extension/include/svc/MapProcessMemoryEx.h`:
```c
#define MAPEXFLAGS_PRIVATE BIT(0)  // Maps as PRIVATE (0xBB05)
// Without flag: Maps as SHARED (0x5806)
```

**Emulator implementation**:
```cpp
Result SVC::MapProcessMemoryEx() {
    const Handle dst_handle = GetReg(1);
    const u32 dst_addr = GetReg(2);
    const Handle src_handle = GetReg(3);
    const u32 src_addr = GetReg(4);
    const u32 size = GetReg(5);
    const u32 flags = GetReg(6);
    
    bool is_private = (flags & 1) != 0;
    
    // Map the source process memory into the destination process
    auto src_process = kernel.GetProcess(src_handle);
    auto dst_process = kernel.GetProcess(dst_handle);
    
    if (!src_process || !dst_process) {
        return ResultInvalidHandle;
    }
    
    // Use VMManager to create the mapping
    // ...
    
    return ResultSuccess;
}
```

#### SVC 0xB1 — CopyHandle

**Purpose**: Copy a handle from one process to another.

**From** `ref/Luma3DS/k11_extension/source/svc/CopyHandle.c`:
```c
Result CopyHandle(u32 *out, Handle handle, u32 target)
{
    // Creates a new handle in the target process
    // that refers to the same kernel object
}
```

**Emulator implementation**:
```cpp
Result SVC::CopyHandle() {
    const VAddr out_handle_addr = GetReg(1);
    const Handle src_handle = GetReg(2);
    const Handle dst_process_handle = GetReg(3);
    
    auto src_process = kernel.GetCurrentProcess();
    auto dst_process = kernel.GetProcess(dst_process_handle);
    
    if (!dst_process) {
        return ResultInvalidHandle;
    }
    
    // Get the object from source handle table
    auto object = src_process->handle_table.GetGeneric(src_handle);
    if (!object) {
        return ResultInvalidHandle;
    }
    
    // Create handle in destination process
    Handle new_handle;
    auto result = dst_process->handle_table.Create(&new_handle, object);
    if (result.IsError()) {
        return result;
    }
    
    memory.Write32(out_handle_addr, new_handle);
    return ResultSuccess;
}
```

#### SVC 0xB2 — TranslateHandle

**Purpose**: Translate a handle through IPC session translation.

**From** `ref/Luma3DS/k11_extension/source/svc/TranslateHandle.c`:
```c
Result TranslateHandle(u32 *out, Handle handle, u32 target, Handle session)
{
    // Translates a handle from one process's namespace
    // to another through a session
}
```

**Emulator implementation**:
```cpp
Result SVC::TranslateHandle() {
    const VAddr out_handle_addr = GetReg(1);
    const Handle src_handle = GetReg(2);
    const Handle dst_process_handle = GetReg(3);
    const Handle session_handle = GetReg(4);
    
    // Similar to CopyHandle but respects session translation
    // For HLE, we can simplify this
    
    auto src_process = kernel.GetCurrentProcess();
    auto dst_process = kernel.GetProcess(dst_process_handle);
    
    if (!dst_process) {
        return ResultInvalidHandle;
    }
    
    auto object = src_process->handle_table.GetGeneric(src_handle);
    if (!object) {
        return ResultInvalidHandle;
    }
    
    Handle new_handle;
    auto result = dst_process->handle_table.Create(&new_handle, object);
    if (result.IsError()) {
        return result;
    }
    
    memory.Write32(out_handle_addr, new_handle);
    return ResultSuccess;
}
```

#### SVC 0xB3 — ControlProcess

**Purpose**: Control process state (schedule threads, etc.).

**From** `ref/Luma3DS/k11_extension/source/svc/ControlProcess.c`:
```c
#define PROCESSOP_TERMINATE          1
#define PROCESSOP_SCHEDULE_THREADS   2
#define PROCESSOP_SET_VIRTUAL_MEMORY 3
#define PROCESSOP_GET_FRONT_PROCESS  4
#define PROCESSOP_SET_PROCESS_CANCELLATION 5
#define PROCESSOP_GET_COMMAND_OPTION 6
#define PROCESSOP_SET_PROCESS_PERMISSION 7
```

**Emulator implementation**:
```cpp
Result SVC::ControlProcess() {
    const Handle process_handle = GetReg(1);
    const u32 action = GetReg(2);
    const u32 arg1 = GetReg(3);
    const u32 arg2 = GetReg(4);
    
    auto process = kernel.GetProcess(process_handle);
    if (!process) {
        return ResultInvalidHandle;
    }
    
    switch (action) {
    case 1: // TERMINATE
        process->Terminate();
        break;
    case 2: // SCHEDULE_THREADS
        // arg1: 0=resume, 1=suspend
        // arg2: thread predicate address
        // For emulation, we can iterate threads
        break;
    case 4: // GET_FRONT_PROCESS
        // Return the current foreground process
        break;
    // ... other cases
    }
    
    return ResultSuccess;
}
```

### 3.2 Hooked Official SVCs

Luma3DS also hooks several official SVCs to add functionality:

#### SVC 0x01 — ControlMemory Hook

Adds extended memory control flags for plugin memory strategies.

#### SVC 0x03 — ExitProcess Hook

Adds plugin cleanup on process exit.

#### SVC 0x08 — CreateThread Hook (N3DS only)

Adds thread creation extensions for N3DS.

#### SVC 0x2A — GetSystemInfo Hook

Returns additional system info values (N3DS detection, Luma3DS config).

#### SVC 0x32 — SendSyncRequest Hook

Adds IPC interception for service access logging.

#### SVC 0x3C — Break Hook

Enhanced break handling with debug event support.

**Emulator implementation**: These hooks can be implemented as conditional logic in the existing SVC handlers.

---

## 4. System Modules Replacement

### 4.1 Loader — Title Patching

From `ref/Luma3DS/sysmodules/loader/source/patcher.c`:

The loader applies patches at load time based on title ID and configuration:

```c
void patchCode(u64 progId, u16 progVer, u8 *code, u32 size, 
               u32 textSize, u32 roSize, u32 dataSize,
               u32 roAddress, u32 dataAddress)
{
    // Apply game-specific patches
    // 1. LayeredFS redirection
    // 2. Locale emulation
    // 3. Language override
    // 4. CPU speed override
    // 5. Splash screen
    // 6. Unit info patch
    // 7. Thread redirection
    // 8. DSi ext filter
}
```

**Emulator implementation**:

```cpp
// In the loader, after loading the NCCH
void Loader::PatchTitle(u64 title_id, u8* code, u32 size) {
    // Check Luma3DS config for enabled patches
    if (config.patch_games) {
        // Apply LayeredFS redirection
        if (config.layeredfs_enabled) {
            ApplyLayeredFSRedirect(title_id, code, size);
        }
        
        // Apply locale emulation
        if (config.locale_emulation_enabled) {
            ApplyLocaleEmulation(title_id, code, size);
        }
        
        // Apply CPU speed override
        if (config.cpu_speed_override != 0) {
            ApplyCPUSpeedOverride(code, size, config.cpu_speed_override);
        }
    }
}
```

### 4.2 PM — Process Management

From `ref/Luma3DS/sysmodules/pm/`:

The PM handles:
- Process creation/termination
- Memory limit management
- Break-on-start for debugging

**Emulator implementation**:
```cpp
class PMService : public ServiceFramework<PMService> {
    void BreakOnStart(Kernel::HLERequestContext& ctx) {
        // Register a process to break on start
        // Used by GDB server
        const Handle process_handle = GetHandle(1);
        auto process = kernel.GetProcess(process_handle);
        if (process) {
            process->SetBreakOnStart(true);
        }
    }
};
```

### 4.3 SM — Service Manager

From `ref/Luma3DS/sysmodules/sm/`:

The SM removes access control restrictions.

**Emulator implementation**: Already done — the emulator's SM doesn't enforce access control.

---

## 5. Rosalina Overlay Menu

### 5.1 Menu System

From `ref/Luma3DS/sysmodules/rosalina/source/menu.c`:

Rosalina runs as a separate thread that polls button input. When the menu combo is detected, it pauses the game and shows the overlay.

**Emulator implementation**:

```cpp
class RosalinaMenu {
    bool is_visible = false;
    u32 menu_combo = 0x2DF; // L+Down+Select
    
    void CheckButtonCombo() {
        // Poll input state
        auto input = HIDService::GetButtonState();
        if ((input & menu_combo) == menu_combo) {
            ToggleMenu();
        }
    }
    
    void ToggleMenu() {
        if (!is_visible) {
            // Pause all threads
            PauseGameThreads();
            // Show menu
            is_visible = true;
        } else {
            // Resume all threads
            ResumeGameThreads();
            // Hide menu
            is_visible = false;
        }
    }
    
    void Render() {
        if (!is_visible) return;
        
        // Draw menu items:
        // - Cheats
        // - Process list
        // - Debugger
        // - Screen filters
        // - Input redirection
        // - NTP sync
        // - Screenshot
        // - Save settings
    }
};
```

### 5.2 Menu Features

From `ref/Luma3DS/sysmodules/rosalina/source/menus/`:

| Feature | File | Implementation |
|---------|------|----------------|
| Process List | `process_list.c` | Show running processes |
| Cheats | `cheats.c` | Execute cheat codes |
| Debugger | `debugger.c` | GDB server control |
| Screen Filters | `screen_filters.c` | Display correction |
| Miscellaneous | `miscellaneous.c` | Various settings |
| N3DS Options | `n3ds.c` | N3DS-specific settings |
| System Config | `sysconfig.c` | System configuration |

---

## 6. Plugin System (3GX)

### 6.1 Plugin Loading

From `ref/Luma3DS/sysmodules/rosalina/source/plugin/plgloader.c`:

Plugins are loaded in several steps:

1. **Parse 3GX header** (`3gx.c`)
2. **Allocate memory** (`memoryblock.c`)
3. **Map into process** (via MapProcessMemoryEx SVC)
4. **Set up plugin header** at `0x07000000`
5. **Start plugin threads**
6. **Handle events** (home, swap, exit)

### 6.2 Plugin Memory Strategies

From `ref/Luma3DS/sysmodules/rosalina/source/plugin/memoryblock.c`:

| Strategy | Description | O3DS | N3DS |
|----------|-------------|------|------|
| SWAP | Swap to/from SD card | ✅ | ❌ |
| EXTENDED | Use extended memory | ❌ | ✅ |
| MODE3 | Use 8MB mode 3 | ✅ | ❌ |
| EXTITLE | Use extra title memory | ✅ | ✅ |

### 6.3 Emulator Implementation

**Phase 1 — Stub the service** (prevent crashes):
```cpp
// Already partially implemented
// Return "disabled" for IsEnabled
// Return success for all commands
```

**Phase 2 — Full plugin loading** (requires ARM code execution):
```cpp
class PluginLoader {
    bool LoadPlugin(u64 title_id, const std::string& path) {
        // 1. Parse 3GX file
        PluginHeader header;
        if (!Parse3GX(path, header)) {
            return false;
        }
        
        // 2. Allocate memory in target process
        auto process = kernel.GetProcess(title_id);
        u32 plugin_addr = AllocatePluginMemory(process, header.size);
        
        // 3. Map plugin code
        MapPluginCode(process, plugin_addr, header);
        
        // 4. Set up plugin header at 0x07000000
        SetupPluginHeader(plugin_addr, header);
        
        // 5. Start plugin threads
        StartPluginThreads(process, header.entry_point);
        
        return true;
    }
};
```

**Note**: 3GX plugins are native ARM code. They require JIT/native execution to work. The emulator's interpreter cannot execute them directly.

---

## 7. Boot Process & FIRM Loading

### 7.1 Luma3DS Boot Chain

```
boot9strap → arm9loaderhax → Luma3DS arm9 → Patch NATIVE_FIRM → 
  Inject KIPs (sm, pm, loader, pxi, rosalina) → Boot patched FIRM →
  Kernel starts → K11 extension loads → System modules start →
  Home Menu launches
```

### 7.2 FIRM Patching

From `ref/Luma3DS/arm9/source/`:

The arm9loaderhax patches NATIVE_FIRM in memory:
1. Patches Process9 code
2. Injects KIPs (Kernel Initial Processes)
3. Hooks kernel startup
4. Loads k11_extension

**Emulator implementation**:

The emulator already loads the Home Menu directly. To support Luma3DS:

```cpp
class Luma3DSBoot {
    void PatchFIRM(FirmwareImage& firm) {
        // 1. Patch Process9
        PatchProcess9(firm);
        
        // 2. Inject KIPs
        InjectKIP(firm, "sm.kip");
        InjectKIP(firm, "pm.kip");
        InjectKIP(firm, "loader.kip");
        InjectKIP(firm, "pxi.kip");
        InjectKIP(firm, "rosalina.kip");
        
        // 3. Hook kernel startup
        HookKernelStartup(firm);
        
        // 4. Load k11_extension
        LoadK11Extension(firm);
    }
};
```

### 7.3 KIP Injection

KIPs are pre-built system modules that replace the originals. The emulator needs to:

1. **Load KIP files** from the Luma3DS directory
2. **Replace the corresponding HLE modules** with KIP implementations
3. **Map KIP memory** into the correct address ranges
4. **Start KIP threads** with the correct priorities

---

## 8. Feature Parity Checklist

### Core Features

| Feature | Status | Implementation |
|---------|--------|----------------|
| Custom SVCs (0x80-0xB3) | 🔴 Missing | Add to SVC table |
| SM access control removal | ✅ Done | Already unrestricted |
| PM break-on-start | 🔴 Missing | Add to PM service |
| Loader title patching | 🔴 Missing | Add patchCode() |
| 3DSX loading | 🔴 Missing | Add 3DSX loader |
| K11 extension hooks | 🔴 Missing | Hook SVC handlers |

### Rosalina Features

| Feature | Status | Implementation |
|---------|--------|----------------|
| Overlay menu | 🔴 Missing | Add menu thread |
| Button combo detection | 🔴 Missing | Poll HID input |
| Process list | 🔴 Missing | Query process manager |
| Cheats | ✅ Done | Existing cheat engine |
| GDB server | 🔴 Missing | Add GDB stub |
| Screen filters | 🔴 Missing | Add post-processing |
| Input redirection | ✅ Done | Existing HID input |
| NTP sync | 🔴 Missing | Add NTP client |
| Screenshot | 🔴 Missing | Add screenshot capture |
| Error display | 🔴 Missing | Add err:d service |

### Plugin Features

| Feature | Status | Implementation |
|---------|--------|----------------|
| plg:ldr service | ⚠️ Stubbed | Full implementation needed |
| 3GX parsing | 🔴 Missing | Add 3GX parser |
| Plugin memory mapping | 🔴 Missing | Add MapProcessMemoryEx |
| Plugin event system | 🔴 Missing | Add event handlers |
| Plugin swap (O3DS) | 🔴 Missing | Not needed on N3DS/iOS |

### Game Modding Features

| Feature | Status | Implementation |
|---------|--------|----------------|
| LayeredFS | 🔴 Missing | Add file redirection |
| Locale emulation | 🔴 Missing | Add locale config |
| CPU speed override | 🔴 Missing | Add CPU config |
| Language override | 🔴 Missing | Add language config |

---

## 9. Implementation Order

### Phase 1 — Core SVCs (1-2 weeks)

**Goal**: Implement all custom SVCs so homebrew can call them without crashing.

1. SVC 0x90 — ConvertVAToPA
2. SVC 0x91-0x94 — Cache operations
3. SVC 0xB1 — CopyHandle
4. SVC 0xB2 — TranslateHandle
5. SVC 0xB3 — ControlProcess
6. SVC 0xA0 — MapProcessMemoryEx
7. SVC 0xA1 — UnmapProcessMemoryEx
8. SVC 0x80 — CustomBackdoor (stub)

### Phase 2 — System Module Extensions (2-3 weeks)

**Goal**: Extend existing system modules with Luma3DS functionality.

1. PM: Add break-on-start support
2. PM: Add memory limit management
3. SM: Verify unrestricted access (already done)
4. Loader: Add patchCode() framework
5. Loader: Add LayeredFS support
6. Loader: Add locale emulation

### Phase 3 — Rosalina Menu (2-3 weeks)

**Goal**: Implement the overlay menu for in-game use.

1. Menu thread and button combo detection
2. Process list display
3. Screenshot capture
4. Save/load settings
5. Screen filter post-processing
6. NTP time sync
7. Error display service

### Phase 4 — Plugin System (3-4 weeks)

**Goal**: Full 3GX plugin support.

1. plg:ldr service full implementation
2. 3GX file parser
3. Plugin memory allocation
4. Plugin thread management
5. Plugin event system
6. Plugin swap (optional, O3DS only)

### Phase 5 — GDB Server (2-3 weeks)

**Goal**: Remote debugging support.

1. GDB stub implementation
2. Breakpoint support
3. Watchpoint support
4. Memory read/write
5. Register inspection
6. Thread control

### Phase 6 — Advanced Features (2-4 weeks)

**Goal**: Complete feature parity.

1. LayeredFS file redirection
2. Locale emulation
3. CPU speed override
4. System module replacement
5. Chainloading (FIRM loading)
6. NTR card emulation

---

## 10. Technical Deep Dives

### 10.1 SVC Implementation Pattern

Every custom SVC follows this pattern:

```cpp
Result SVC::CustomSVC() {
    // 1. Read parameters from registers
    const u32 param1 = GetReg(1);
    const u32 param2 = GetReg(2);
    
    // 2. Validate inputs
    if (!IsValidAddress(param1)) {
        return ResultInvalidAddress;
    }
    
    // 3. Perform operation
    auto result = PerformOperation(param1, param2);
    
    // 4. Write output to registers
    SetReg(1, result.output);
    
    // 5. Return result code
    return result.status;
}
```

### 10.2 IPC Hook Pattern

Luma3DS hooks IPC to intercept service calls:

```cpp
void SVC::SendSyncRequestHook() {
    const Handle session_handle = GetReg(1);
    
    // Pre-IPC hook
    OnBeforeIPC(session_handle);
    
    // Original IPC
    SendSyncRequest(session_handle);
    
    // Post-IPC hook
    OnAfterIPC(session_handle);
}
```

### 10.3 Memory Mapping Pattern

For MapProcessMemoryEx and similar:

```cpp
Result SVC::MapProcessMemoryEx() {
    const u32 flags = GetReg(6);
    bool is_private = (flags & MAPEXFLAGS_PRIVATE) != 0;
    
    if (is_private) {
        // Map as PRIVATE — process gets its own copy
        return MapPrivate(process, dst_addr, src_addr, size);
    } else {
        // Map as SHARED — changes are visible to both processes
        return MapShared(process, dst_addr, src_addr, size);
    }
}
```

### 10.4 Plugin Event System

From `ref/Luma3DS/sysmodules/rosalina/source/plugin/plgloader.c`:

```c
typedef enum {
    PLG_NONE = 0,
    PLG_OK,
    PLG_WAIT,
    PLG_HOME_ENTER,
    PLG_HOME_EXIT,
    PLG_ABOUT_TO_SWAP,
    PLG_ABOUT_TO_EXIT,
} PLG_Event;

typedef enum {
    PLG_CFG_NONE = 0,
    PLG_CFG_RUNNING,
    PLG_CFG_INHOME,
    PLG_CFG_EXITING,
} PLG_CfgStatus;
```

Events are signaled through shared memory and arbiters:

```cpp
class PluginEventSystem {
    void NotifyEvent(PLG_Event event, bool signal) {
        // Write event to shared memory
        *plgEventPA = event;
        
        if (signal) {
            // Signal the arbiter
            kernel.SignalArbiter(arbiter, plgEventPA);
        }
    }
    
    void WaitForReply() {
        // Wait for plugin to acknowledge
        *plgReplyPA = PLG_WAIT;
        kernel.WaitArbiter(arbiter, plgReplyPA, timeout);
    }
};
```

### 10.5 LayeredFS Implementation

LayeredFS redirects file reads to SD card overrides:

```cpp
class LayeredFS {
    std::unordered_map<std::string, std::string> redirections;
    
    bool LoadRedirections(u64 title_id) {
        // Load from /luma/titles/<title_id>/fs/
        std::string base_path = fmt::format("/luma/titles/{:016X}/fs/", title_id);
        
        for (auto& entry : directory_iterator(base_path)) {
            std::string original = entry.path().filename();
            std::string redirect = entry.path().string();
            redirections[original] = redirect;
        }
        
        return true;
    }
    
    std::string RedirectPath(const std::string& original) {
        auto it = redirections.find(original);
        if (it != redirections.end()) {
            return it->second;
        }
        return original;
    }
};
```

---

## Appendix A: File References

| Luma3DS File | Purpose | Key Functions |
|--------------|---------|---------------|
| `k11_extension/source/svc.c` | SVC table setup | `buildAlteredSvcTable()` |
| `k11_extension/source/main.c` | Kernel extension entry | MMU setup, relocation |
| `k11_extension/source/svc/ControlService.c` | SVC 0xB0 | Service stealing |
| `k11_extension/source/svc/CopyHandle.c` | SVC 0xB1 | Handle copying |
| `k11_extension/source/svc/TranslateHandle.c` | SVC 0xB2 | Handle translation |
| `k11_extension/source/svc/ControlProcess.c` | SVC 0xB3 | Process control |
| `k11_extension/source/svc/MapProcessMemoryEx.c` | SVC 0xA0 | Memory mapping |
| `sysmodules/rosalina/source/main.c` | Rosalina entry | Service registration |
| `sysmodules/rosalina/source/menu.c` | Menu system | Button combo, rendering |
| `sysmodules/rosalina/source/plugin/plgloader.c` | Plugin loader | CMD 1-14 handlers |
| `sysmodules/rosalina/source/plugin/plgldr.c` | Plugin client | IPC protocol |
| `sysmodules/rosalina/source/plugin/3gx.c` | 3GX format | Header parsing |
| `sysmodules/rosalina/source/plugin/memoryblock.c` | Plugin memory | Memory strategies |
| `sysmodules/rosalina/source/errdisp.c` | Error display | Fatal error handling |
| `sysmodules/rosalina/source/input_redirection.c` | Input redirect | Network HID |
| `sysmodules/rosalina/source/ntp.c` | NTP sync | Time synchronization |
| `sysmodules/rosalina/source/gdb.c` | GDB server | Remote debugging |
| `sysmodules/loader/source/patcher.c` | Title patching | `patchCode()` |
| `sysmodules/loader/source/3dsx.c` | 3DSX loading | Homebrew support |
| `sysmodules/pm/source/pmapp.c` | Process management | Process control |
| `sysmodules/sm/source/srv_pm.c` | Service management | Access control |

## Appendix B: SVC Quick Reference

| SVC | Name | Luma3DS | Status |
|-----|------|---------|--------|
| 0x01 | ControlMemory | Hooked | 🔴 Missing hook |
| 0x03 | ExitProcess | Hooked | 🔴 Missing hook |
| 0x08 | CreateThread | Hooked (N3DS) | 🔴 Missing hook |
| 0x29 | GetHandleInfo | Hooked | 🔴 Missing hook |
| 0x2A | GetSystemInfo | Hooked | 🔴 Missing hook |
| 0x2B | GetProcessInfo | Hooked | 🔴 Missing hook |
| 0x2C | GetThreadInfo | Hooked | 🔴 Missing hook |
| 0x2D | ConnectToPort | Hooked | 🔴 Missing hook |
| 0x32 | SendSyncRequest | Hooked | 🔴 Missing hook |
| 0x3C | Break | Hooked | 🔴 Missing hook |
| 0x59 | SetGpuProt | Custom | 🔴 Missing |
| 0x5A | SetWifiEnabled | Custom | 🔴 Missing |
| 0x7B | Backdoor | Custom | 🟡 Stub |
| 0x7C | KernelSetState | Hooked | 🔴 Missing |
| 0x80 | CustomBackdoor | Custom | 🟡 Stub |
| 0x90 | ConvertVAToPA | Custom | 🔴 Missing |
| 0x91 | FlushDataCacheRange | Custom | 🔴 Missing |
| 0x92 | FlushEntireDataCache | Custom | 🔴 Missing |
| 0x93 | InvalidateICacheRange | Custom | 🔴 Missing |
| 0x94 | InvalidateEntireICache | Custom | 🔴 Missing |
| 0xA0 | MapProcessMemoryEx | Custom | 🔴 Missing |
| 0xA1 | UnmapProcessMemoryEx | Custom | 🔴 Missing |
| 0xA2 | ControlMemoryEx | Custom | 🔴 Missing |
| 0xA3 | ControlMemoryUnsafe | Custom | 🔴 Missing |
| 0xB0 | ControlService | ✅ Implemented | ✅ Done |
| 0xB1 | CopyHandle | Custom | 🔴 Missing |
| 0xB2 | TranslateHandle | Custom | 🔴 Missing |
| 0xB3 | ControlProcess | ✅ Implemented | ✅ Done |

---

*Last updated: 2026-08-23*
*Reference: ref/Luma3DS (full source tree)*
