# Official 3DS networking and the emulator's coverage

Sources: `ref/ctr/sources/processes/nwm/` (the NWM module), `ref/ctr/include/nn/nwm/`, `ref/ctr/include/nn/uds/`, `ref/ctr/include/nn/socket/`, `ref/ctr/include/nn/ssl/`, `ref/3dbrew/wiki/NWM Services.md` and friends, plus the emulator's `src/core/hle/service/{nwm,soc,ssl,frd,http}/`.

## The official stack (bottom → top)

1. **Wi-Fi hardware** — the 3DS has an 802.11b/g (11b/g) radio with 4 power levels. Two wireless modes:
   - **Infrastructure mode** (NWM `nwm::Infra`): connects to a real access point, gives the console an IP.
   - **Local wireless (UDS — "User Datagram System" over 802.11)** (`nwm::Uds`): ad-hoc 802.11 with its own beaconing, used by local multiplayer, StreetPass (`nwm::Cec`/CECD data), and Download Play.
2. **NWM module** (`ref/ctr/sources/processes/nwm/`): the single system process owning the radio. Services:
   - `nwm::Uds` (local wireless), `nwm::Infra`, `nwm::Cec` (StreetPass relay), `nwm::Ext` (download play / extended), `nwm::Soc` (the SOC socket proxy), `nwm::Tst` (test).
   - UDS details in `include/nn/uds/CTR/uds_*.h`: network description, SSID (16 bytes), beacon, connection IDs, max 16 nodes, channel 1/6/11.
3. **SOC** (`soc:U`) — the socket service. BSD-style sockets (`socket.h`): `socket`, `bind`, `connect`, `listen`, `accept`, `send`/`recv`, `sendto`/`recvfrom`, `getsockopt`/`setsockopt`, `select`/`poll`, `getaddrinfo`, plus privileged raw/packet access via `soc:U`'s admin commands.
4. **SSL** (`ssl:C`) — TLS stack on top of SOC, with a CA cert store (`include/nn/ssl/ssl_CertStore.h`, `ssl_CrlStore.h`), client certs (`ssl_ClientCert.h`), and `ssl_C` service commands. Used by `http` (HTTP/HTTPS service) for eShop/NNID traffic.
5. **Friends** (`frd:u`/`frd:a`, module `friends/` in `sources/processes/friends/`) — friend lists, presence, and the **friend code seed** (`LocalFriendCodeSeed`) that ties the console identity together; rides over the same infra network.
6. **HTTP** (`http:C`) — the HTTP client service built on SSL/SOC.
7. **StreetPass (CECD)** — periodic background scans for other consoles' beacons and exchange of "boxes" over NWM CEC.

## What the emulator implements (HLE) — `src/core/hle/service/`

| Area | Files | Status |
|---|---|---|
| `nwm::Uds` local wireless | `nwm/nwm_uds.cpp`, `nwm/uds_beacon.cpp`, `nwm/uds_connection.cpp`, `nwm/uds_data.cpp` | **Implemented** — Citra's full UDS implementation: real sockets over the host LAN with a beacon broadcast; supports local multiplayer between emulator instances. Some commands still stubbed (`SetMaxSendDelay`, `Flush`, `SetProbeResponseParam`, `ScanOnConnection`; beacon key stubbed with all-zeros). |
| `nwm::Infra` | `nwm/nwm_inf.cpp` | Present (used by SOC/HTTP paths). |
| `nwm::Cec` | `nwm/nwm_cec.cpp` | Present. |
| `nwm::Ext` / `nwm::Sap` / `nwm::Tst` | `nwm/nwm_ext.cpp`, `nwm_sap.cpp`, `nwm_tst.cpp` | Present. |
| `nwm::Soc` | `nwm/nwm_soc.cpp` | Present. |
| `soc:U` (sockets) | `soc/soc_u.cpp` | **Implemented** — maps BSD calls to the host via `SOCKET` abstraction (host `socket()` etc.). A few `getsockopt`/`setsockopt` options stubbed. |
| `ssl:C` | `ssl/ssl_c.cpp` | Thin — mostly returns success stubs; no real TLS handshake via host (some builds route through host OpenSSL). |
| `frd:u`/`frd:a` | `frd/frd_u.cpp`, `frd/frd_a.cpp`, `frd/frd.cpp` | **Implemented** (friends list, presence), with several stubbed commands (17 stub sites). Real friend-code/NNID networking (servers) is not emulated. |
| `http:C` | `http/http_c.cpp` | Present — host HTTP requests. |
| `ac:u` (Wi-Fi connection) | `ac/ac_u.cpp` | Present; 12 stub sites (connection states are largely faked). |
| `ndm:u` (daemon) | `ndm/` | Present. |
| `cecd:u` | `cecd/cecd.cpp` | Present; 6 stub sites (the StreetPass box transfer mechanics). |
| `dlp` (download play) | `dlp/` | Present; 4 stub sites. |

The 3DS's actual online ecosystem (NNID, eShop, SpotPass, friend servers, multi-user online play through the real network) is **not** emulated — the emulator covers the *local* stack (UDS LAN multiplayer, host sockets) and stubs the rest.

## Key official details worth reusing (from the SDK)

- **UDS beacon format** — `include/nn/uds/CTR/uds_InfoElement.h`, `uds_NetworkDescription.h`: the emulator's `uds_beacon.cpp` recreates this; the SDK confirms element ordering and the (stubbed) encryption key material.
- **SOC BSD semantics** — `include/nn/socket/socket_*.h`: confirm exact `sockaddr` layout (16-byte `sockaddr_in`, big-endian on the wire) and error codes (`socket_Result.h`) the emulator's `soc_u.cpp` should return.
- **SSL cert store** — `include/nn/ssl/ssl_CertStore.h`: CA cert count limits and store layout; useful for implementing `ssl:C` properly instead of stubbing.
- **NWM infra connection state machine** — `sources/processes/nwm/CTR/nwm_InfraImpl.cpp` / `nwm_InfraServer.cpp`: the exact connect→DHCP→IP-assigned sequence `ac:u`/`nwm::Infra` HLE should mimic (currently mostly success-stubbed).
- **StreetPass boxes** — `sources/processes/cecd/CTR/cec_MessageBoxAdmin.cpp` (see `leaked-sdk-findings.md` §2.2): the emulator's `cecd` stubs could be filled with the real box format for cross-emulator StreetPass over the LAN (reusing the UDS beacon machinery).

## What the SDK module source actually implements (the emulator's reference)

`ref/ctr/sources/processes/nwm/CTR/` is the full NWM module:

- **`nwm_InfraImpl.cpp`** — infrastructure mode: `OpenMode`/`CloseMode`, `Connect(ssid, bssid, channel, security, cancelEvent)`, `ConnectWps`, `Disconnect`, `SetPowerSaveMode`/`GetPowerSaveMode`, `AddWpsIe`/`RemoveWpsIe`. The connect flow goes through `PublicInfra::SetConnectTarget` → association → DHCP → IP — the state machine the emulator's `ac:u`/`nwm::Infra` should mimic instead of instant success.
- **`uds/`** — the local-wireless core: `nwm_UdsImpl.cpp` (`InitializeWithVersion` (creates the shared-memory status block), `CreateNetwork`/`CreateNetwork2` (SSID + passphrase → `ConnectionManager::CreateNetwork`), `ConnectNetwork`/`ConnectNetwork2`, `EjectClient`, `EjectSpectator`, `UpdateNetworkAttribute`, `DestroyNetwork`, `SetProbeResponseParam`), plus `uds_ConnectionManager.cpp`, `uds_IeManager.cpp` (info-element/beacon build), `uds_NodeManager.cpp`, `uds_SendDataManager.cpp`, `uds_ReceiveThread.cpp`. The emulator's `nwm_uds.cpp` + `uds_beacon.cpp` recreate this over host multicast — the SDK confirms `SetProbeResponseParam`/`Flush` semantics for the remaining stubs.
- **`nwm_CecImpl.cpp`** — StreetPass: scan/beacon exchange over UDS, feeding `cecd`. The emulator's `nwm_cec.cpp` is the HLE counterpart.
- **`nwm_SapImpl.cpp`** — the SAP (access-point) layer: `StartScan(scanParam, channelScanTime, IeMatchInfo, ssidMatchLength)`, `Connect` with `BssIndication`, master-mode `Disconnect` with 802.11 reason codes.
- **socket client** — `sources/libraries/socket/socket_Berkeley.cpp`, `socket_User.cpp`, `socket_Privileged.cpp`: the BSD API the `soc:U` service serves; `socket_InetUtils.cpp` for sockaddr conversions. Error codes in `include/nn/socket/socket_Result.h`.
- **ssl module** — `sources/processes/ssl/`: `ssl_ConnectionServer.cpp`/`ssl_ConnectionSession.cpp` (TLS connections), `ssl_CertStoreServer.cpp` + `ssl_CertificationServer.cpp` + `ssl_CrlServer.cpp` (CA certs, client certs, CRLs), `ssl_InternalCertifications.cpp` (the built-in Nintendo root certs). This is the full surface the emulator's `ssl_c.cpp` shell should implement.

## Networking gaps (actionable)

1. **`ssl:C` real TLS** — route through host TLS (OpenSSL) instead of stub-success; needed for any title doing HTTPS (eShop, NNID) against real or self-hosted servers.
2. **`ac:u` connection semantics** — mimic the real connect→DHCP→configured state machine instead of instant success (some titles check intermediate states).
3. **StreetPass over LAN** — implement CECD box exchange on top of the working UDS multicast; the SDK gives the box format.
4. **Download Play (DLP)** — real host-network mirroring of a title to other emulator instances (currently stubbed).
5. **Friend-code / NNID servers** — out of scope for local emulation (requires Nintendo's servers or a private reimplementation like Pretendo), but the SDK's `friends/` module source documents the exact client protocol a Pretendo-style server would need to match.
6. **Un-stub UDS**: `SetMaxSendDelay`, `Flush`, `SetProbeResponseParam`, `ScanOnConnection` — small, well-defined from `uds_*.h`.
7. **Beacon crypto** — the all-zeros beacon key (`uds_beacon.cpp`) is fine for LAN, but real-key derivation from the console seed is documented in the SDK if interop with real hardware is ever wanted (requires the actual keys).
