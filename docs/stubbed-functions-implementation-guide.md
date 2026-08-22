# Stubbed HLE Functions: What They Are and How to Implement Them (with 3DBrew)

Applies to: Azahar (Citra-derived 3DS emulator), iOS + desktop cores.
Reference corpus: `ref/3dbrew/wiki/` (local English 3DBrew archive — load the `3dbrew`
skill for navigation recipes).

## 1. What a "stub" is in this codebase

HLE services live under `src/core/hle/service/<service>/`. Each service registers a
table of command handlers. A handler is a **stub** in one of three forms:

1. **Explicit stub** — the function exists but just logs `(STUBBED)` and returns
   `ResultSuccess` (or canned values), e.g.:

   ```cpp
   void Module::Interface::Foo(Kernel::HLERequestContext& ctx) {
       IPC::RequestParser rp(ctx);
       ...
       LOG_WARNING(Service_X, "(STUBBED) called ...");
       IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
       rb.Push(ResultSuccess);
   }
   ```

2. **`nullptr` handler** — the table entry has no function, e.g.
   `{0x0006, nullptr, "GetAccountInfo"}`. `ServiceFrameworkBase::HandleSyncRequest`
   calls `ReportUnimplementedFunction`, which logs
   `unknown / unimplemented function 'X'` and replies with a **1-word success
   response** (`cmd_buf[0]=header(1,0)`, `cmd_buf[1]=0`). The client then reads the
   response's remaining words/buffers as **garbage** — this is frequently worse than
   an explicit error, because the guest cannot tell the call failed.

3. **Partial implementation** — the handler parses inputs and pushes a result, but
   never fills the output buffer it was given (e.g. ACT `GetAccountInfo` historically
   pushed `ResultSuccess` while leaving the mapped output buffer untouched, and built
   the response with `MakeBuilder(1, 0)` instead of echoing the buffer descriptor
   `MakeBuilder(1, 2)` + `PushMappedBuffer`).

The three failure modes above are the reason stubs can cause guest crashes. **Note:**
for the Home Menu boot failure (`RSL 0xF8A14D5F` — Level 31 Fatal, Summary 5
InvalidState, Module 83 ACT, Desc 351) the fatal is actually thrown by the **ACT
module running as real firmware (LLE)**, not by the HLE stub — see section 7. Stubs
matter once the module runs in HLE instead.

## 2. How 3DBrew documents the real behavior

Every service command has a wiki page named `PORT_Command.md` (e.g.
`ACTU_GetAccountDataBlock.md`, `PTM_RegisterAlarmClient.md`,
`SSLC_RootCertChainAddDefaultCert.md`, `FS_ControlArchive.md`), and service overviews
(`ACT Services.md`, `PTM Services.md`, `BOSS Savegame.md`, ...). Each command page
documents:

- **Request** — every index word: command header, params, handle/buffer descriptors.
- **Response** — every index word incl. the echoed buffer descriptors.
- **Description** — semantics, plus links to shared data structures ("see
  DataBlocks"), which are often on the service overview page.

Key pages for common work: `Services.md` (overview), `Error codes.md` (result-code
layout: Level/Summary/Module/Desc bit fields + module table), `System SaveData.md`
(archive IDs), and per-service data-structure pages (`ACT Services.md#DataBlocks`,
`BOSS Savegame.md`, `Config Savegame.md`).

## 3. How to implement a stub (step by step)

1. **Find the stub.** `grep -rn "STUBBED" src/core/hle/service/<svc>/` or find
   `nullptr` entries in the `functions[]` table. The log tells you which ones the
   guest actually hits: `unknown / unimplemented function 'X': port='port'`.
2. **Open the 3DBrew page.** `PORT_Command.md` for the command, or
   `X Services.md` for the overview. Read the request/response index words.
3. **Copy the request parsing** from the wiki into the handler:
   `rp.Pop<u32>()`, `rp.Pop<u8>()`, `rp.PopPID()`, `rp.PopObject<...>()`,
   `rp.PopMappedBuffer()` in the exact order listed.
4. **Implement the semantics.** Fill output buffers via
   `buffer.Write(data, 0, size)` / `buffer.Read(...)`, set return values, and use the
   service's error enum (`ErrCodes`/`ErrDescriptions` in the service's
   `*_errors.h`) for failures.
5. **Build the response correctly.** The response header count must match the wiki:
   `rb.MakeBuilder(normal_words, translate_words)` and **always echo mapped buffers**
   with `rb.PushMappedBuffer(buffer)` — omitting the descriptor is itself a stub bug
   that corrupts the client's IPC parsing. See `cfg.cpp` for a correct reference
   (`GetConfigInfoBlk2` pattern).
6. **Return real errors, not fake success.** A stub returning `ResultSuccess` with
   empty data makes the guest *believe* the call worked. If you cannot implement the
   behavior, returning the documented error code is often safer than fake success.
7. **Cross-check result codes** against `Error codes.md` (module table: 0 Common,
   17 FS, 32 AM, 51 Applet, 62 BOSS, 83 ACT, ...) and the service's error enum.

## 4. Prioritization guidance (from the Home Menu boot failure)

The Home Menu (and most system titles) hit stubs in this order; fix the ones that
break control flow or data validity first:

1. **Functions the guest treats as mandatory** and whose empty/garbage response is
   fatal — e.g. ACT `Initialize`/`GetAccountInfo`/`GetCommonInfo`. Fix response
   structure AND return real data.
2. **Functions with observable side effects the guest relies on** — e.g. PTM
   `RegisterAlarmClient` (guest believes an alarm is registered), HTTP/SSL root-cert
   setup (`http:C CreateRootCertChain`, `RootCertChainAddDefaultCert`).
3. **Archive/DB operations the guest re-initializes on first boot** — BOSS
   sysdata (`00010034`: `BOSS_A.db`, `BOSS_SS.db`, `BOSS_SV.db`), CFG
   (`00010017`), CEC (`00010026`). Creating valid empty databases per
   `BOSS Savegame.md`/`System SaveData.md` prevents noisy first-boot errors.

### 4.1 Specific log6 findings (`ref/log6/azahar_log.txt`)

| Stub hit | Port | Consequence |
|---|---|---|
| `CreateRootCertChain` / `RootCertChainAddDefaultCert` | `http:C` | Returned success; guest continues. Harmless for offline boot. |
| `RegisterAlarmClient` | `ptm:s` | Returned success; guest continues. Harmless for offline boot. |
| BOSS `BOSS_SV.db`/`BOSS_SS.db` open + `FS:ControlArchive` (action 0 = commit) | FS sysdata `00010034` | Missing DBs are normal first-boot; guest deletes/recreates. Harmless once files exist. |
| **ACT module init (LLE)** | — | **Fatal (PID 11, ACT 351) is thrown by the real ACT module running as an LLE process** (forced by the `ONLINE_LLE_REQUIRED` hack when booting the Home Menu). Its sysdata `00010038` (`persisid.dat`/`transid.dat`/`uuid.dat`/`account.dat`) only exists on a real-console dump. See section 7. |
| ACT `GetAccountInfo` (0x0006) | `act:u` / `act:a` | Returns success with an untouched output buffer. When ACT runs in HLE (see section 7) this must zero-fill the block so the guest's account init gets a sane "no NNID" response. |

## 5. ACT `GetAccountInfo` data blocks (what "real data" means)

From `ACT Services.md#DataBlocks` — `GetAccountInfo(account_slot, size, block_id)`
writes `size` bytes of the requested block into the output buffer:

| BlkID | Size | Content |
|---|---|---|
| 0x5 | 0x4 | PersistentId |
| 0x6 | 0x8 | TransferableIdBase |
| 0x7 | 0x60 | Mii CFLStoreData (all-zero = none) |
| 0x8 | 0x11 | AccountId (ASCII NNID, empty for console-only) |
| 0xA | 0x4 | Birth Date (u16 year, u8 month, u8 day) |
| 0x11 | 0xA0 | Full account data (see below) |
| 0x12 | 0x4 | Account server types (4 × u8, 0 = production) |
| 0x13 | 0x1 | Gender (0 = unspecified) |
| 0x14 | 0x1 | LastAuthenticationResult (0 = none) |
| 0x1A | 0x1 | IsCommitted (1 = local account committed) |
| 0x2C | 0x2 | Age |
| others | — | Return zeros for unimplemented blocks |

Full account data (BlkID 0x11, 0xA0 bytes):

```
0x00 u32 PersistentID
0x04 u32 padding
0x08 u64 TransferableIDBase
0x10 0x60 Mii CFLStoreData
0x70 0x16 UTF-16 Mii display name (10 chars + NUL)
0x86 0x11 ASCII NNID account ID
0x97 0x01 padding
0x98 0x04 Birth Date
0x9C 0x04 PrincipalID (0 = no NNID)
```

A fresh console account = nonzero PersistentId + TransferableIdBase, `IsCommitted=1`,
all NNID fields zero.

## 6. Reference implementations to copy from

- Correct response-with-buffer pattern: `src/core/hle/service/cfg/cfg.cpp`
  (`GetConfigInfoBlk2`).
- Full service implementations that already exist in this fork and can be used as
  style templates: `src/core/hle/service/boss/online_service.cpp` (real BOSS DB
  read/write), `src/core/hle/service/news/news.cpp`, `cecd.cpp` (sysdata
  self-format on first open).
- The upstream "reference" ports (`ref/AzaharPlus`, `ref/azahar`) are **not** ahead
  of this fork for ACT — their `act.cpp` is byte-identical (stub). Any
  implementation here is new work guided by 3DBrew, not a port.

## 7. LLE system modules and why they crash on NUS-only installs (the real Home Menu fix)

### 7.1 Mechanism

`src/common/hacks/hack_list.cpp` ships an `ONLINE_LLE_REQUIRED` hack with
`mode = FORCE` for several system titles — including all six Home Menu title IDs
(`0x0004003000008F02` = USA, etc.). Booting the Home Menu therefore **forces** the
"online" system modules — ACT (`0004013000003802`), BOSS (`...3402`), CECD
(`...2602`), FRD (`...3202`), NIM (`...2C02`) — to load as **LLE processes**: real
firmware running on the emulated CPU, instead of the HLE stubs. `Service::Init`
(`src/core/hle/service/service.cpp`) does this via `AttemptLLE`, which consults
`Settings::values.enable_required_online_lle_modules` (default **false**) *or* the
hack override.

### 7.2 Why the ACT module fatals

On boot the real ACT module (PID 11 — the first LLE process, per the load order in
`ref/log6/azahar_log.txt`) registers with SRV, sets up `http:C` root certs and a
`ptm:s` alarm, then opens its system save data (`sysdata/00010038`) directly via
`fs:USER`: `persisid.dat`, `transid.dat`, `uuid.dat`, `account.dat`, `hash.dat`.

- `ref/log4`: the files are **missing** → the module throws fatal ACT 351 at
  `ADR 0x00100522`.
- `ref/log6`: the files exist (emulator-created) but the **contents are rejected**
  (persistent ID / account data must match the console's unique data) → same fatal
  at the same address.

On a real console this sysdata is written at the factory and is derived from the
console's unique data (`SecureInfo_A`, `LocalFriendCodeSeed_B` in `nand/rw/sys/`),
which the emulator **never generates** — it only reads them if the user supplies a
full NAND dump (`HW::UniqueData`; `CFG_S::GetLocalFriendCodeSeedData` returns
`NotFound` without them). A NUS-only system-files install (CIA download, as in
AzaharPlus's `SystemFilesViewModel` → `downloadTitleFromNus`) provides neither, so
the real ACT module can never initialize. Neither reference (`ref/azahar`,
`ref/AzaharPlus`) creates ACT save data; their `act.cpp` is byte-identical to this
fork's stub, and their install flow creates no sysdata.

### 7.3 Root cause (from the official system-module source)

`ref/ctr` is the leaked Nintendo 3DS SDK and contains the **actual source of the
system modules**. Reading `ref/ctr/sources/processes/act/` (the ACT module)
decodes the fatal precisely:

- `act_Main.cpp` → `SessionManager` boot steps 9–11 open the ACT save data via
  `AccountManager`, which **self-initializes** on first boot (its KVS text files
  are created by the module itself — see `save/act_KeyValueStore.cpp`; a missing
  or malformed file is logged as "The administrative file not found. Created." and
  recreated). So the save data was never the real blocker.
- Step 12 is `SystemInfoManager::CreateSingleton` → `Initialize()`
  (`util/act_SystemInfoManager.cpp`), which reads the **console device
  information**:
  1. `ReadDeviceCert` → `nn::am::GetDeviceCert` (`AMNet:GetDeviceCert`,
     0x0818) — needs a 384-byte device cert.
  2. `ReadDeviceId` → `nn::am::GetDeviceId` (`AM:GetDeviceID`, 0x000A).
  3. `ReadSerialId` → `nn::cfg::CTR::system::GetSerialNo`
     (`CFG:SecureInfoGetSerialNo`, 0x0408 on cfg:sys).
  4. `ReadSystemVersion` → `nn::am::GetProgramInfos` for the region's NUP
     version title (`NVer`, e.g. US `0x000400DB00016302` — installed with the
     system files).
- Any of the first three failing is converted to `ResultDeviceInfoReadError`
  (desc 351 → display `022-5351`): **that is the fatal in log4/log6.**

The emulator returns `NotFound` for all three when the OTP dump / `SecureInfo_A`
are absent, because on real hardware they are derived from the console's OTP
(`AM:GetDeviceID` reads `otp.GetDeviceID()`; `GetDeviceCert` serializes the CT
cert built from OTP; the serial lives in `SecureInfo_A`). The ACT module stores
these values locally (base64-encodes the cert, keeps the ID/serial in memory); it
never validates them against Nintendo's servers at boot.

### 7.4 Fix implemented — synthesize the console device identity

Instead of blocking LLE, the emulator now **synthesizes** the device identity so
the real ACT module can boot on a NUS-only install (no OTP/SecureInfo dump):

1. **`src/core/hle/service/am/am.cpp` — `AM:GetDeviceID`:** when the OTP is
   invalid, derive a stable device ID from the CFG console unique ID
   (`Service::CFG::GetModule(system)->GetConsoleUniqueId()`), which is generated
   and persisted by the emulator at config init. LLE modules (e.g. ACT) get a
   real, stable value instead of `NotFound`.
2. **`src/core/hle/service/am/am.cpp` — `AM:GetDeviceCert`:** when the CT cert
   (from OTP) is invalid, build a structurally valid 384-byte self-signed ECC
   device certificate (`Certificate::BuildECC` + `GenerateKeyPair`), matching
   `NN_ACT_DEVICE_CERT_SIZE = 384`. The ACT module only base64-encodes and stores
   it — it never verifies it locally.
3. **`src/core/hle/service/cfg/cfg.cpp` — `CFG:SecureInfoGetSerialNo`:** when
   `SecureInfo_A` is absent, write a stable serial (`"SYS"` + 9 digits derived
   from the console unique ID) instead of `NotFound`. Matches the format the ACT
   module's `ReadSerialNumber` expects (3 alpha chars + decimal digits).
4. **`src/core/hle/service/service.cpp` — `AttemptLLE`:** the previous HLE
   fallback gate was **removed** — with the synthesized identity the LLE modules
   (ACT/BOSS/CECD/FRD/NIM) can boot, so recommended-LLE again loads real firmware
   for everyone (with or without a dump), restoring online-capable behavior for
   dump users while making NUS-only installs boot.
5. **`src/ios/AzaharBridge/ios_bridge.mm`:** removed the fabricated ACT save data
   (`00010038` binary `persisid.dat`/`transid.dat`/`uuid.dat`/`account.dat`) —
   the real ACT module self-initializes its KVS text files on first boot, and the
   binary blobs were garbage it would reject and recreate anyway.
6. **`src/core/hle/service/act/act.cpp` — `GetAccountInfo`:** zero-fills the
   requested block in the output buffer (HLE path), so the Home Menu's account
   library gets a valid "fresh console, no NNID" response.

Boot flow on a NUS-only install now: the five online modules load as LLE, the ACT
module reads its device info (all synthesized), self-initializes its save data,
and the Home Menu proceeds.

### 7.5 Remaining limits / future work

The synthesized identity is **locally sufficient** — the ACT module boots and the
Home Menu's account library sees a fresh console with no NNID. Real online
features (NNID registration, eShop, friends) still require Nintendo's servers and
real console unique data (OTP / `SecureInfo_A` / `LocalFriendCodeSeed_B` from a
full NAND dump), since the synthesized cert is not signed by Nintendo's CA. Both
refs treat that as out-of-scope.
