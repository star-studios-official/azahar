# Stubbed & Unimplemented Services — Implementation Guide

> **Purpose**: Step-by-step guide for implementing every stubbed and unimplemented IPC command, with exact file paths to the leaked Nintendo SDK (`ref/ctr`), 3DBrew wiki (`ref/3dbrew`), and official Nintendo developer SDK (`ref/ctr_sdk`) as reference sources.

---

## Table of Contents

1. [Reference Source Index](#1-reference-source-index)
2. [Tier 1: Fix Known Crashes](#2-tier-1-fix-known-crashes)
3. [Tier 2: Home Menu Completeness](#3-tier-2-home-menu-completeness)
4. [Tier 3: Networking & Online (Pretendo/Nimbus)](#4-tier-3-networking--online-pretendonimbus)
5. [Tier 4: Nice-to-Have](#5-tier-4-nice-to-have)
6. [Implementation Patterns & Templates](#6-implementation-patterns--templates)

---

## 1. Reference Source Index

### Leaked SDK (`ref/ctr/`)

| Path | Contents |
|------|----------|
| `ref/ctr/sources/libraries/ssl/` | SSL client library — IPC command tags, connection flow |
| `ref/ctr/sources/libraries/nwm/CTR/` | NWM client library — Socket, UDS, INF, EXT command tags |
| `ref/ctr/sources/libraries/http/` | HTTP client library — all HTTP IPC commands |
| `ref/ctr/sources/processes/ssl/` | SSL sysmodule server-side — process management, cert stores |
| `ref/ctr/sources/processes/GraphicsServer/CTR/` | GSP sysmodule — display manager, interrupt relay, command queue |
| `ref/ctr/sources/processes/nwm/` | NWM sysmodule server-side |
| `ref/ctr/sources/processes/cfg/CTR/` | CFG sysmodule — SecureInfo, LFCS, certificate handling |
| `ref/ctr/sources/libraries/drivers/cfg/CTR/` | CFG drivers — RSA verification, certificate validation |
| `ref/ctr/include/nn/ssl/` | SSL type definitions, constants, IPC message format |
| `ref/ctr/include/nn/nwm/CTR/` | NWM type definitions |
| `ref/ctr/include/nn/http/` | HTTP type definitions |
| `ref/ctr/include/nn/gsp/` | GSP type definitions |

### 3DBrew Wiki (`ref/3dbrew/`)

| Path | Contents |
|------|----------|
| `ref/3dbrew/wiki/SSL Services.md` | Full SSL command table with headers and descriptions |
| `ref/3dbrew/wiki/SSLC_*.md` | Individual SSL command documentation |
| `ref/3dbrew/wiki/NWM Services.md` | NWM service command tables |
| `ref/3dbrew/wiki/NWM Shared Memory.md` | NWM mbuf pool structure |
| `ref/3dbrew/wiki/GSPGPU_*.md` | GSP GPU command documentation |
| `ref/3dbrew/wiki/CTCert.md` | CT certificate format |
| `ref/3dbrew/wiki/OTP Registers.md` | OTP memory layout |
| `ref/3dbrew/wiki/Services API.md` | Service list and descriptions |

### Official SDK (`ref/ctr_sdk/`)

| Path | Contents |
|------|----------|
| `ref/ctr_sdk/` | Official Nintendo developer documentation (non-leaked) |

---

## 2. Tier 1: Fix Known Crashes

### 2.1 ✅ `nwm::SOC` CMD 0x0009 — `GetMbufPoolInformation`

**Status**: FIXED (commits pending)

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/libraries/nwm/CTR/nwm_Socket.cpp` (line ~105: `GetMbufPoolInformation`)
- 3DBrew: `ref/3dbrew/wiki/NWM Services.md` (nwm::SOC section, CMD 0x00090000)

**Implementation**:
```cpp
// Already implemented in src/core/hle/service/nwm/nwm_soc.cpp
// Returns: sharedmem_size(0x22000), sharedmem_handle, event_handle
// See nwm_soc.cpp for full implementation
```

**Key details from leaked SDK**:
```cpp
// ref/ctr/sources/libraries/nwm/CTR/nwm_Socket.cpp
nn::Result Socket::GetMbufPoolInformation(
    nn::Handle* phSharedMemory, size_t* pSize, nn::Handle* phEvent)
{
    // Sends TAG_GET_MBUF_POOL_INFORMATION (0x0009) with no params
    // Response: size at position 2, handles at positions 4 and 5
}
```

---

### 2.2 `NWM::EXT` CMD 0x0008 — `ControlWirelessEnabled`

**Status**: Unimplemented (nullptr)

**Crash Risk**: WiFi toggle freeze

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/libraries/nwm/CTR/nwm_Ext.cpp`
- 3DBrew: `ref/3dbrew/wiki/NWM Services.md` (nwm::EXT section)

**IPC Command**: `0x00080040` — CMD 0x08, takes u32 `enabled` parameter

**Implementation Strategy**:
1. The command enables/disables the WiFi radio
2. For emulation, we can stub it to always return success (WiFi is always "on" in emulation)
3. The u32 parameter is a boolean: 1=enable, 0=disable

```cpp
// In src/core/hle/service/nwm/nwm_ext.cpp
void NWM_EXT::ControlWirelessEnabled(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 enabled = rp.Pop<u32>();
    LOG_INFO(Service_NWM, "called, enabled={}", enabled);
    // Emulated WiFi is always available
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

**Register in function table**:
```cpp
{0x0008, &NWM_EXT::ControlWirelessEnabled, "ControlWirelessEnabled"},
```

---

### 2.3 `GSP::GPU` CMD 0x000A — `RegisterInterruptEvents`

**Status**: Unimplemented (nullptr)

**Crash Risk**: Black screen on titles using GSP interrupt relay

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/processes/GraphicsServer/CTR/gsp_InterruptRelay.cpp`
- Leaked SDK: `ref/ctr/sources/processes/GraphicsServer/CTR/gsp_InterruptRelay.h`
- 3DBrew: `ref/3dbrew/wiki/GSPGPU_RegisterInterruptRelayQueue.md`

**IPC Command**: `0x000A0042` — CMD 0x0A, takes event handle + interrupt mask

**Implementation Strategy**:
The GSP interrupt relay is a mechanism for the GPU service to notify applications about events (VBlank, display transfer complete, etc.). The implementation needs to:

1. Store the event handle and interrupt mask
2. Signal the event when the corresponding GPU interrupt fires
3. Return the current interrupt flags

```cpp
// In src/core/hle/service/gsp/gsp_gpu.cpp
void GSP_GPU::RegisterInterruptEvents(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 interrupt_mask = rp.Pop<u32>();
    auto interrupt_event = rp.PopObject<Kernel::Event>();
    const u32 caller_pid = rp.Pop<u32>();

    LOG_INFO(Service_GSP, "called, interrupt_mask=0x{:08X}", interrupt_mask);

    // Store the event and mask for this process
    // When GPU interrupts fire, signal the event with the interrupt flags

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

**Key details from leaked SDK**:
- `ref/ctr/sources/processes/GraphicsServer/CTR/gsp_InterruptRelay.h` shows the `InterruptRelayQueue` class
- The interrupt flags are stored in shared memory at offset 0x0 of the GSP shared page
- VBlank interrupt is bit 0x01 in the interrupt mask

---

### 2.4 SVC 0x2E–0x31 — `SendSyncRequest1–4`

**Status**: Unimplemented (nullptr in SVC table)

**Crash Risk**: LLE DSP module crash, batched IPC failure

**Reference Files**:
- 3DBrew: `ref/3dbrew/wiki/SVC.md` (SvcSendSyncRequestLight section)
- Leaked SDK: `ref/ctr/sources/libraries/` (nn::svc usage)

**Implementation Strategy**:
These are batched versions of `SendSyncRequest`. They allow sending multiple IPC requests in a single SVC call. The DSP module uses these for efficiency.

```cpp
// In src/core/hle/kernel/svc.cpp
Result SVC::SendSyncRequestLight(Handle handle) {
    // Same as SendSyncRequest but with lighter context switching
    // For HLE, just delegate to the normal path
    return SendSyncRequest(handle);
}

Result SVC::SendSyncRequest1(Handle handle) {
    return SendSyncRequest(handle);
}

Result SVC::SendSyncRequest2(Handle handle) {
    return SendSyncRequest(handle);
}

Result SVC::SendSyncRequest3(Handle handle) {
    return SendSyncRequest(handle);
}

Result SVC::SendSyncRequest4(Handle handle) {
    return SendSyncRequest(handle);
}
```

**Register in SVC table**:
```cpp
{0x2E, &SVC::Wrap<&SVC::SendSyncRequestLight, 0x2E>, "SendSyncRequest1", 1000},
{0x2F, &SVC::Wrap<&SVC::SendSyncRequestLight, 0x2F>, "SendSyncRequest2", 1000},
{0x30, &SVC::Wrap<&SVC::SendSyncRequestLight, 0x30>, "SendSyncRequest3", 1000},
{0x31, &SVC::Wrap<&SVC::SendSyncRequestLight, 0x31>, "SendSyncRequest4", 1000},
```

---

## 3. Tier 2: Home Menu Completeness

### 3.1 `SRV` CMD 0x0004 — `UnregisterService`

**Status**: Unimplemented (nullptr)

**Reference Files**:
- 3DBrew: `ref/3dbrew/wiki/SRV.md`
- Leaked SDK: `ref/ctr/sources/processes/` (SRV server-side)

**Implementation Strategy**:
```cpp
void SRV::UnregisterService(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto port = rp.PopObject<Kernel::ServerPort>();
    LOG_DEBUG(Service_SRV, "called, port={}", port ? port->GetName() : "null");
    // Remove the service from the service manager
    if (port) {
        system.ServiceManager().UnregisterService(port->GetName());
    }
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

---

### 3.2 `PTM` CMD 0x0001 — `RegisterAlarmClient`

**Status**: Unimplemented (nullptr)

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/libraries/ptm/` (if exists)
- 3DBrew: `ref/3dbrew/wiki/PTM_Services.md`

**Implementation Strategy**:
Register an alarm callback that fires at a specified time. For emulation, return a dummy alarm handle.

```cpp
void PTM_S_Common::RegisterAlarmClient(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    // Read alarm time and other params
    LOG_WARNING(Service_PTM, "called (stubbed)");
    // Create a dummy alarm event
    auto alarm_event = system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "PTM alarm");
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 1);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(alarm_event);
}
```

---

### 3.3 `NWM::INF` CMD 0x0006 — `RecvBeaconBroadcastData`

**Status**: Unimplemented (nullptr)

**Crash Risk**: WiFi indicator shows disconnected

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/libraries/nwm/CTR/nwm_InfraAPI.cpp`
- 3DBrew: `ref/3dbrew/wiki/NWMINF_RecvBeaconBroadcastData.md`

**Implementation Strategy**:
Returns WiFi beacon data for nearby access points. For emulation, return empty scan results.

```cpp
void NWM_INF::RecvBeaconBroadcastData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    // Read input size and scan parameters
    LOG_INFO(Service_NWM, "called (returning empty scan results)");
    
    // Return empty beacon data structure:
    // MaxOutputSize | TotalWritten(0xC) | TotalEntries(0)
    std::vector<u8> output(0xC, 0);
    *reinterpret_cast<u32*>(output.data() + 4) = 0xC; // TotalWritten
    *reinterpret_cast<u32*>(output.data() + 8) = 0;   // TotalEntries
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushStaticBuffer(std::move(output), 0);
}
```

**BeaconDataReply structure** (from 3DBrew):
```
Offset 0x0: MaxOutputSize (u32)
Offset 0x4: TotalWritten (u32) — 0xC when no entries
Offset 0x8: TotalEntries (u32) — 0 for no results
Offset 0xC: Beacon entries (variable)
```

---

## 4. Tier 3: Networking & Online (Pretendo/Nimbus)

### 4.1 SSL:C — Complete Service Implementation

The SSL service is the **biggest blocker for Pretendo/Nimbus**. It uses RSA BSAFE internally (hence the "ssl:C" name = "SSL-C" from BSAFE).

**Architecture** (from leaked SDK):
- SSL uses a **main session** for global operations (cert stores, contexts)
- Each `CreateContext` opens a **dedicated connection session** for that context
- `InitializeConnectionSession` must be called on the dedicated session
- All subsequent commands for that context go through the dedicated session

**Source Files**:
- Leaked SDK Client: `ref/ctr/sources/libraries/ssl/ssl_Connection.cpp`, `ssl_ClientCert.cpp`
- Leaked SDK Server: `ref/ctr/sources/processes/ssl/ssl_ClientProcess.cpp`
- Leaked SDK Types: `ref/ctr/include/nn/ssl/ssl_ConnectionIpc.h`
- 3DBrew: `ref/3dbrew/wiki/SSL Services.md`

**Command Tag → IPC CMD mapping** (from `ssl_ConnectionIpc.h`):

| Tag | IPC CMD | Name | Parameters | Response |
|-----|---------|------|------------|----------|
| 0x01 | 0x0001 | InitializeGeneralSession | — | — |
| 0x02 | 0x0002 | NewClient (CreateContext) | socket, verifyOpt, serverName | handle |
| 0x03 | 0x0003 | CreateCertStore | — | certStoreId |
| 0x04 | 0x0004 | DestroyCertStore | certStoreId | — |
| 0x05 | 0x0005 | AddCertToCertStore | certStoreId, certData | certId |
| 0x06 | 0x0006 | AddInternalCertToCertStore | certStoreId, internalCertName | certId |
| 0x07 | 0x0007 | RemoveCertFromCertStore | certStoreId, certId | — |
| 0x0D | 0x000D | OpenClientCertContext | certData, privateKeyData | certId |
| 0x0E | 0x000E | OpenDefaultClientCertContext | — | certId |
| 0x0F | 0x000F | CloseClientCertContext | certId | — |
| 0x11 | 0x0011 | GenerateRandomData | size | randomBytes |
| 0x12 | 0x0012 | InitializeConnectionSession | connectionHandle | — |
| 0x13 | 0x0013 | DoHandshake | connectionHandle | — |
| 0x14 | 0x0014 | DoHandshakeGetServerCert | connectionHandle | certSize, certNum |
| 0x15 | 0x0015 | Read | connectionHandle, size | bytesRead |
| 0x16 | 0x0016 | Peek | connectionHandle, size | bytesRead |
| 0x17 | 0x0017 | Write | connectionHandle, size | bytesWritten |
| 0x18 | 0x0018 | ContextSetRootCertChain | connectionHandle, certStoreId | — |
| 0x19 | 0x0019 | ContextSetClientCert | connectionHandle, certId | — |
| 0x1B | 0x001B | ContextClearOpt | connectionHandle, excludeOpt | — |
| 0x1C | 0x001C | ContextGetProtocolCipher | connectionHandle | version, cipher, bits |
| 0x1E | 0x001E | Shutdown (DestroyContext) | connectionHandle | — |
| 0x1F | 0x001F | ContextInitSharedmem | connectionHandle, sharedmem | — |

**Implementation Order**:
1. `Initialize` (already done)
2. `GenerateRandomData` (already done)
3. `CreateCertStore` / `AddCertToCertStore` / `AddInternalCertToCertStore` / `DestroyCertStore`
4. `CreateContext` (NewClient) — creates an SSL context, returns handle
5. `InitializeConnectionSession` — opens dedicated session for context
6. `ContextSetRootCertChain` — associates cert chain with context
7. `ContextSetClientCert` — associates client cert with context
8. `StartConnection` (DoHandshake) — performs TLS handshake using host OpenSSL
9. `Read` / `Write` — encrypted data transfer via host OpenSSL
10. `DestroyContext` (Shutdown) — cleanup

**Key Implementation Detail**: For each SSL connection, the emulator should:
1. Map the 3DS socket to a host OpenSSL `BIO` or `SSL` object
2. Use the host's OpenSSL library for actual TLS operations
3. Map SSL error codes to 3DS error codes (from `ssl_Const.h`)

**Internal CA Certificates** (from `ssl_Const.h`):
```cpp
CACERT_NINTENDO_CA           = 0x01  // Nintendo CA
CACERT_NINTENDO_CA_G2        = 0x02  // Nintendo CA G2
CACERT_NINTENDO_CA_G3        = 0x03  // Nintendo CA G3
CACERT_NINTENDO_CLASS2_CA    = 0x04  // Nintendo Class2 CA
CACERT_NINTENDO_CLASS2_CA_G2 = 0x05  // Nintendo Class2 CA G2
CACERT_NINTENDO_CLASS2_CA_G3 = 0x06  // Nintendo Class2 CA G3
CACERT_PUBLIC_CA_1           = 0x07  // Public CA 1
CACERT_PUBLIC_CA_2           = 0x08  // Public CA 2
CACERT_PUBLIC_CA_3           = 0x09  // Public CA 3
CACERT_PUBLIC_CA_4           = 0x0A  // Public CA 4
```

**SSLOpt Flags** (from 3DBrew):
```
0x001 (bit 0): Verify Common Name (CN)
0x002 (bit 1): Verify RootCA
0x004 (bit 2): Verify date
0x008 (bit 3): Verify cert chain
0x010 (bit 4): Verify subject alt name
0x020 (bit 5): Verify cert EV
0x200 (bit 9): Makes certification validation always succeed (IGNORE mode)
0x800 (bit 11): Disable TLSv1.1 (fallback to TLSv1.0)
```

**Error Codes** (from 3DBrew):
```
0xD8A0B801: Not an SSL connection
0xD840B802: EWOULDBLOCK on read
0xD840B803: EWOULDBLOCK on write
0xD840B807: EWOULDBLOCK on StartConnection
0xD8A0B805: Syscall error (connection closed)
0xD8A0B806: End-of-stream
0xD8A0B814: Untrusted RootCA
0xD8A0B836: RootCertChain handle not found
```

---

### 4.2 SSL:C — Quick-Start Minimal Implementation

For games that use the `VERIFY_IGNORE` option (bit 9), we can skip certificate verification entirely:

```cpp
// Minimal SSL implementation that bypasses verification
class SSLC : public ServiceFramework<SSLC> {
    // For each connection, store:
    struct SSLContext {
        int socket_fd;          // 3DS socket descriptor
        u32 verify_options;     // SSLOpt flags
        std::string server_name;
        // Host OpenSSL state
        SSL* ssl = nullptr;
        BIO* bio = nullptr;
    };
    
    std::unordered_map<u32, std::unique_ptr<SSLContext>> contexts;
    u32 next_context_id = 1;
    
    void CreateContext(Kernel::HLERequestContext& ctx);
    void InitializeConnectionSession(Kernel::HLERequestContext& ctx);
    void StartConnection(Kernel::HLERequestContext& ctx);
    void Read(Kernel::HLERequestContext& ctx);
    void Write(Kernel::HLERequestContext& ctx);
    void Shutdown(Kernel::HLERequestContext& ctx);
};
```

---

### 4.3 HTTP:C — Missing Commands

**Status**: Mostly implemented. Missing commands:

| CMD | Name | Implementation |
|-----|------|---------------|
| 0x07 | GetRequestError | Return error code from last failed request |
| 0x0D | SetProxy | Store proxy config, apply to host connections |
| 0x0F | SetBasicAuthorization | Store auth header, prepend to requests |
| 0x10 | SetSocketBufferSize | Store buffer size, apply to sockets |
| 0x27 | SetClientCert | Store client cert for mutual TLS |
| 0x2C | SetSSLClearOpt | Store SSL clear options |
| 0x35 | SetDefaultProxy | Store default proxy |
| 0x36 | ClearDNSCache | Clear host DNS cache |

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/libraries/http/` (HTTP IPC command tags)
- 3DBrew: `ref/3dbrew/wiki/HTTP Services.md`

---

## 5. Tier 4: Nice-to-Have

### 5.1 `GSP::GPU` CMDs 0x0D/0x0E/0x0F — Display Transfer / Texture Copy / Memory Fill

**Status**: Unimplemented (nullptr)

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/processes/GraphicsServer/CTR/gsp_DisplayManager.h`
- 3DBrew: `ref/3dbrew/wiki/GSPGPU_SetDisplayTransfer.md`

**Implementation Strategy**:
These commands transfer data between VRAM and linear memory. In the Vulkan/OpenGL renderer, this is handled differently (direct framebuffer rendering). For LLE GPU or titles that bypass the renderer, implement as memory copies:

```cpp
void GSP_GPU::SetDisplayTransfer(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 src_addr = rp.Pop<u32>();
    const u32 src_width = rp.Pop<u16>();
    const u32 src_height = rp.Pop<u16>();
    const u32 dst_addr = rp.Pop<u32>();
    const u32 dst_width = rp.Pop<u16>();
    const u32 dst_height = rp.Pop<u16>();
    const u32 flags = rp.Pop<u32>();
    
    // Copy from src to dst with format conversion
    // This is a simplified version — full implementation needs
    // format conversion (RGB8→RGBA8, etc.)
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

### 5.2 `DSP::DSP` Semaphore Commands

**Status**: Unimplemented (nullptr)

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/processes/DSP/` (if exists)
- 3DBrew: `ref/3dbrew/wiki/DSP_Services.md`

**Note**: These are only needed for LLE DSP. With HLE DSP (default), these commands are not called.

### 5.3 `PTM` Alarm/Sleep Commands

**Status**: Unimplemented (nullptr)

**Reference Files**:
- Leaked SDK: `ref/ctr/sources/libraries/ptm/`
- 3DBrew: `ref/3dbrew/wiki/PTM_Services.md`

**Commands**: SetRtcAlarm, GetRtcAlarm, CancelRtcAlarm, ReplySleepQuery, etc.

**Strategy**: For sleep/wake commands, return success with no action (emulator doesn't sleep). For RTC alarms, create kernel events that fire at the specified time.

### 5.4 SVC 0x04–0x11 — Process/Thread Affinity

**Status**: Unimplemented (nullptr)

**Reference Files**:
- 3DBrew: `ref/3dbrew/wiki/SVC.md`

**Strategy**: Return dummy values or ignore the calls. The emulator doesn't have real CPU cores to pin to.

---

## 6. Implementation Patterns & Templates

### Pattern 1: Simple Stub (Return Success)

For commands that are called but whose results aren't used:

```cpp
void ServiceClass::StubCommand(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_XXX, "called (stubbed)");
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

### Pattern 2: Stub with Dummy Return Value

For commands that return data the caller expects:

```cpp
void ServiceClass::StubWithValue(Kernel::HLERequestContext& ctx) {
    LOG_WARNING(Service_XXX, "called (stubbed)");
    IPC::RequestParser rp(ctx);
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push<u32>(0); // Dummy return value
}
```

### Pattern 3: Handle-Based Resource

For commands that create and return kernel objects:

```cpp
void ServiceClass::CreateResource(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    
    auto resource = system.Kernel().CreateEvent(
        Kernel::ResetType::OneShot, "ServiceClass resource");
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 1);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(resource);
}
```

### Pattern 4: Shared Memory

For commands that return shared memory handles:

```cpp
void ServiceClass::GetSharedMemory(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    
    auto result = system.Kernel().CreateSharedMemory(
        system.Kernel().GetCurrentProcess(),
        0x1000, // size
        Kernel::MemoryPermission::ReadWrite,
        Kernel::MemoryPermission::ReadWrite);
    
    if (!result.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }
    
    auto shared_mem = std::move(result).Unwrap();
    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push<u32>(0x1000); // size
    rb.PushCopyObjects(shared_mem);
}
```

### Pattern 5: Async/Event-Based

For commands that signal completion asynchronously:

```cpp
void ServiceClass::AsyncOperation(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    
    // Store context for later completion
    auto operation_id = next_operation_id++;
    pending_operations[operation_id] = {
        .ctx = ctx,
        .completion_event = system.Kernel().CreateEvent(
            Kernel::ResetType::OneShot, "AsyncOp")
    };
    
    // Start async work (e.g., network request)
    StartAsyncWork(operation_id);
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}
```

---

## Appendix A: Service Registration Checklist

When implementing a new service command, ensure:

1. **Function declaration** in the header file (`.h`)
2. **Function implementation** in the source file (`.cpp`)
3. **Registration** in the `FunctionInfo` array (replacing `nullptr`)
4. **Include** `common/logging/log.h` for LOG_* macros
5. **Include** `core/hle/ipc_helpers.h` for IPC::RequestParser/RequestBuilder
6. **Use** `IPC::RequestParser rp(ctx);` to read parameters
7. **Use** `rp.MakeBuilder(normal_params, translate_params);` to create response
8. **Push** `ResultSuccess` or appropriate error code as first param
9. **Push** return values in the correct order (matching 3DBrew spec)

## Appendix B: Result Code Quick Reference

```cpp
ResultSuccess           // 0x00000000 — Success
ResultUnknown           // 0xFFFFFFFF — Generic error
ResultInvalidHandle     // Kernel handle error
ResultNotFound          // Object not found
ResultInvalidCombination // Invalid parameters
ResultNotImplemented    // Function not implemented
ResultFatalError        // Fatal system error
```

---

*Last updated: 2026-08-23*
*Reference sources: ref/ctr, ref/3dbrew, ref/ctr_sdk*
