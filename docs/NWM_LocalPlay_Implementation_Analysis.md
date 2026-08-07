# Comprehensive Analysis: NWM/UDS Errors, Local Play Implementation, and CMake Fixes
## Date: August 7, 2026 03:49 UTC

---

## CRITICAL FINDING: ExternalSceneDelegate Missing from Build System

**STATUS**: ❌ **ExternalSceneDelegate.swift and SceneDelegate.swift are NOT compiled**

The files were created but never added to the Xcode project, so they won't be compiled!

**Files Created**:
- `src/ios/AzaharApp/ExternalSceneDelegate.swift` ✅ Created
- `src/ios/AzaharApp/SceneDelegate.swift` ✅ Created

**Files Missing from**: Xcode project sources ❌ Not added

**Impact**: AirPlay/HDMI detection won't work because iOS can't find the scene delegates.

**Solution**: Add these files to the Xcode project manually or ensure they're in the correct directory structure.

---

## Part 1: NWM/UDS Error Analysis

### Current Errors in Azahar Log:
```
[   2.757615] Service.NWM <Error> core/hle/service/nwm/nwm_uds.cpp:NWM_UDS:1713: Network isn't initalized
[  15.647074] Service <Error> unknown / unimplemented function 'SetProbeResponseParam': port='nwm::UDS'
```

### Root Cause Analysis

#### What is NWM::UDS?
- **NWM** = Nintendo Wireless Module (3DS local wireless service)
- **UDS** = User Datagram Service (local multiplayer communication layer)

**Purpose**: Handle 3DS local wireless features:
- Local multiplayer (Pokemon trading, Monster Hunter co-op)
- Download Play (sharing demos)
- StreetPass relay
- Ad-hoc wireless networking

#### From Panda3DS Investigation:
```cpp
// ref/panda3DS/src/core/services/nwm_uds.cpp
void NwmUdsService::initializeWithVersion(u32 messagePointer) {
    Helpers::warn("Initializing NWM::UDS (Local multiplayer, unimplemented)\n");
    
    // Stubbed to fail - returns FailurePlaceholder
    mem.write32(messagePointer + 4, Result::FailurePlaceholder);
}
```

**Key Finding**: Panda3DS intentionally returns failure because implementing NWM requires:
1. Raw 802.11 packet injection (not available on most OSes)
2. Ad-hoc network formation
3. Custom Nintendo wireless protocol
4. Kernel-level network access

#### From Mikage-dev Investigation:
```cpp
// ref/mikage-dev/source/processes/nwm.cpp
static OS::ResultAnd<IPC::StaticBuffer> HandleGetMacAddress(FakeNWM& context, Thread& thread, uint32_t size) {
    // Returns fake MAC address: b0:b1:b2:b3:b4:b5
    thread.WriteMemory(context.soc_static_buffer.addr, 0xb0);
    thread.WriteMemory(context.soc_static_buffer.addr + 1, 0xb1);
    // ...
}
```

**Approach**: Returns fake/dummy responses to prevent crashes, but doesn't actually implement wireless.

#### From NintendoClientsWiki Investigation:

**Local-Protocol.md findings**:
- Uses **LDN** (Local Network Discovery) protocol
- Built on **Pia** networking library
- Uses 802.11 beacon frames with custom Nintendo data
- Requires monitor mode on WiFi adapter (Linux only)
- Not possible on iOS (no raw packet access)

**Architecture**:
```
3DS Local Wireless Stack:
┌────────────────────┐
│   Game Code        │
├────────────────────┤
│   nwm::UDS         │ ← HLE Service (we emulate this)
├────────────────────┤
│   NWM Module       │ ← System module
├────────────────────┤
│   802.11 Driver    │ ← Hardware interface
└────────────────────┘
      ↓ ↑
  Wireless Packets
```

### Why Current Implementation Fails:

**Problem 1**: `Network isn't initalized` error
- **Cause**: Azahar's NWM::UDS returns success but doesn't set internal state flags
- **Location**: `src/core/hle/service/nwm/nwm_uds.cpp:1713`
- **Issue**: Game expects initialized state, but flags not set correctly

**Problem 2**: `SetProbeResponseParam` unimplemented
- **Cause**: Function not implemented in Azahar's NWM service
- **Purpose**: Sets beacon/probe response data for network discovery
- **Impact**: Games trying to host local wireless sessions crash

---

## Part 2: How to Fix NWM Errors (Immediate)

### Fix 1: Proper Initialization Stubbing

**Location**: `src/core/hle/service/nwm/nwm_uds.cpp`

**Add Missing Function Implementations**:

```cpp
void NWM_UDS::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    
    // Create event handle for status notifications
    if (!connection_status_event) {
        connection_status_event = system.Kernel().CreateEvent(
            Kernel::ResetType::OneShot, 
            "NWM::ConnectionStatusEvent"
        );
    }
    
    // Mark as initialized (stub - no real wireless)
    initialized = true;
    network_channel = 6; // Default WiFi channel
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(connection_status_event);
    
    LOG_WARNING(Service_NWM, 
        "NWM::UDS initialized (stubbed - no actual wireless functionality)");
}

void NWM_UDS::SetProbeResponseParam(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x21, 2, 0);
    u32 param1 = rp.Pop<u32>();
    u32 param2 = rp.Pop<u32>();
    
    // Accept parameters but don't act on them (stub)
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    
    LOG_WARNING(Service_NWM, 
        "SetProbeResponseParam({:#x}, {:#x}) - stubbed", param1, param2);
}

void NWM_UDS::BeginHostingNetwork(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    // ... parse network info parameters
    
    // Pretend we started hosting (stub)
    hosting = true;
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    
    LOG_WARNING(Service_NWM, 
        "BeginHostingNetwork - stubbed (no actual network created)");
}

void NWM_UDS::ConnectToNetwork(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    // ... parse network info
    
    // Return "not found" error - can't actually connect
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultCode(ErrorDescription::NotFound, ErrorModule::UDS, 
                       ErrorSummary::NotFound, ErrorLevel::Status));
    
    LOG_WARNING(Service_NWM, 
        "ConnectToNetwork - returning not found (local wireless not supported)");
}
```

**Expected Result**: 
- ✅ Games won't crash when accessing local wireless menu
- ✅ Graceful error message shown to user
- ❌ Local wireless still won't work (requires full implementation)

---

## Part 3: Implementing Local Play Between 2 iPhones

### Challenge: iOS WiFi Restrictions

**Problem**: iOS doesn't allow raw 802.11 packet access
- No monitor mode
- No packet injection
- No ad-hoc network creation
- Sandboxed WiFi access

**Solution**: TCP/IP Tunneling using iOS MultipeerConnectivity

### Architecture: NWM-over-Multipeer Tunneling

```
iPhone 1 (Host)                     iPhone 2 (Client)
┌───────────────────┐               ┌───────────────────┐
│  Pokemon Game     │               │  Pokemon Game     │
│       ↓           │               │       ↓           │
│  nwm::UDS         │               │  nwm::UDS         │
│       ↓           │               │       ↓           │
│  NWM Emulation    │               │  NWM Emulation    │
│       ↓           │               │       ↓           │
│  Multipeer Bridge │               │  Multipeer Bridge │
│       ↓           │               │       ↓           │
│  MultipeerConn.   │               │  MultipeerConn.   │
│       ↓           │               │       ↓           │
│  iOS WiFi/BT      │               │  iOS WiFi/BT      │
└───────┬───────────┘               └───────┬───────────┘
        │                                   │
        └──────── Same LAN Network ─────────┘
```

### Why MultipeerConnectivity?

- ✅ Apple's native peer-to-peer framework
- ✅ Automatic discovery via Bonjour/mDNS
- ✅ Works over WiFi and Bluetooth
- ✅ No server infrastructure needed
- ✅ Handles connection management
- ✅ Built-in encryption and security

### Implementation Components:

#### Component 1: iOS MultipeerConnectivity Manager

**File**: `src/ios/AzaharApp/Utilities/LocalPlayManager.swift` (NEW)

```swift
import MultipeerConnectivity
import Foundation

class LocalPlayManager: NSObject, ObservableObject {
    @Published var isHosting = false
    @Published var isConnected = false
    @Published var connectedPeers: [String] = []
    @Published var availableRooms: [(peerID: MCPeerID, info: [String: String])] = []
    
    private var session: MCSession?
    private var advertiser: MCNearbyServiceAdvertiser?
    private var browser: MCNearbyServiceBrowser?
    private let myPeerID: MCPeerID
    private let serviceType = "azahar-3ds"
    
    override init() {
        myPeerID = MCPeerID(displayName: UIDevice.current.name)
        super.init()
    }
    
    // MARK: - Host (Create Local Wireless Network)
    
    func startHosting(roomName: String, titleId: String, gameTitle: String) {
        stopAll()
        
        session = MCSession(peer: myPeerID, securityIdentity: nil, 
                           encryptionPreference: .none)
        session?.delegate = self
        
        let discoveryInfo: [String: String] = [
            "roomName": roomName,
            "titleId": titleId,
            "gameTitle": gameTitle
        ]
        
        advertiser = MCNearbyServiceAdvertiser(
            peer: myPeerID,
            discoveryInfo: discoveryInfo,
            serviceType: serviceType
        )
        advertiser?.delegate = self
        advertiser?.startAdvertisingPeer()
        
        isHosting = true
        
        AppLogger.info("[LocalPlay] Hosting: \(roomName) | TitleID: \(titleId)")
        
        // Notify C++ that hosting started
        az_nwm_hosting_started()
    }
    
    // MARK: - Client (Join Local Wireless Network)
    
    func startBrowsing() {
        stopAll()
        
        session = MCSession(peer: myPeerID, securityIdentity: nil, 
                           encryptionPreference: .none)
        session?.delegate = self
        
        browser = MCNearbyServiceBrowser(peer: myPeerID, serviceType: serviceType)
        browser?.delegate = self
        browser?.startBrowsingForPeers()
        
        AppLogger.info("[LocalPlay] Browsing for local wireless sessions")
    }
    
    func connectToPeer(_ peerID: MCPeerID) {
        guard let browser = browser, let session = session else { return }
        
        browser.invitePeer(peerID, to: session, withContext: nil, timeout: 10)
        
        AppLogger.info("[LocalPlay] Inviting peer: \(peerID.displayName)")
    }
    
    // MARK: - Send/Receive Packets
    
    func sendPacket(data: Data) {
        guard let session = session, !session.connectedPeers.isEmpty else {
            AppLogger.warning("LocalPlay", message: "No connected peers")
            return
        }
        
        do {
            try session.send(data, toPeers: session.connectedPeers, with: .reliable)
        } catch {
            AppLogger.error("LocalPlay", message: "Send failed: \(error)")
        }
    }
    
    func stopAll() {
        advertiser?.stopAdvertisingPeer()
        browser?.stopBrowsingForPeers()
        session?.disconnect()
        
        advertiser = nil
        browser = nil
        session = nil
        
        isHosting = false
        isConnected = false
        connectedPeers.removeAll()
        availableRooms.removeAll()
    }
}

// MARK: - MCSessionDelegate

extension LocalPlayManager: MCSessionDelegate {
    func session(_ session: MCSession, peer peerID: MCPeerID, 
                 didChange state: MCSessionState) {
        DispatchQueue.main.async {
            switch state {
            case .connected:
                self.connectedPeers.append(peerID.displayName)
                self.isConnected = true
                AppLogger.info("[LocalPlay] Connected: \(peerID.displayName)")
                az_nwm_peer_connected(peerID.displayName)
                
            case .connecting:
                AppLogger.info("[LocalPlay] Connecting: \(peerID.displayName)")
                
            case .notConnected:
                self.connectedPeers.removeAll { $0 == peerID.displayName }
                if self.connectedPeers.isEmpty {
                    self.isConnected = false
                }
                AppLogger.info("[LocalPlay] Disconnected: \(peerID.displayName)")
                az_nwm_peer_disconnected(peerID.displayName)
                
            @unknown default:
                break
            }
        }
    }
    
    func session(_ session: MCSession, didReceive data: Data, 
                 fromPeer peerID: MCPeerID) {
        // Forward packet to C++ NWM emulation
        data.withUnsafeBytes { bytes in
            if let baseAddress = bytes.baseAddress {
                az_nwm_receive_packet(baseAddress, data.count)
            }
        }
    }
    
    func session(_ session: MCSession, didReceive stream: InputStream, 
                 withName streamName: String, fromPeer peerID: MCPeerID) {}
    
    func session(_ session: MCSession, 
                 didStartReceivingResourceWithName resourceName: String, 
                 fromPeer peerID: MCPeerID, with progress: Progress) {}
    
    func session(_ session: MCSession, 
                 didFinishReceivingResourceWithName resourceName: String, 
                 fromPeer peerID: MCPeerID, at localURL: URL?, 
                 withError error: Error?) {}
}

// MARK: - MCNearbyServiceAdvertiserDelegate

extension LocalPlayManager: MCNearbyServiceAdvertiserDelegate {
    func advertiser(_ advertiser: MCNearbyServiceAdvertiser, 
                   didReceiveInvitationFromPeer peerID: MCPeerID, 
                   withContext context: Data?, 
                   invitationHandler: @escaping (Bool, MCSession?) -> Void) {
        AppLogger.info("[LocalPlay] Invitation from: \(peerID.displayName)")
        invitationHandler(true, session)
    }
}

// MARK: - MCNearbyServiceBrowserDelegate

extension LocalPlayManager: MCNearbyServiceBrowserDelegate {
    func browser(_ browser: MCNearbyServiceBrowser, 
                foundPeer peerID: MCPeerID, 
                withDiscoveryInfo info: [String : String]?) {
        DispatchQueue.main.async {
            let roomName = info?["roomName"] ?? "Unknown"
            let titleId = info?["titleId"] ?? "Unknown"
            let gameTitle = info?["gameTitle"] ?? "Unknown Game"
            
            self.availableRooms.append((peerID, info ?? [:]))
            
            AppLogger.info("[LocalPlay] Found: \(roomName) | \(gameTitle)")
        }
    }
    
    func browser(_ browser: MCNearbyServiceBrowser, lostPeer peerID: MCPeerID) {
        DispatchQueue.main.async {
            self.availableRooms.removeAll { $0.peerID == peerID }
            AppLogger.info("[LocalPlay] Lost peer: \(peerID.displayName)")
        }
    }
}
```

#### Component 2: C++ Bridge Functions

**File**: `src/ios/AzaharBridge/ios_bridge.h` (ADD)

```cpp
#ifdef __cplusplus
extern "C" {
#endif

// Local wireless (MultipeerConnectivity) functions
void az_nwm_init_multipeer(void);
void az_nwm_start_hosting(const char* room_name, const char* title_id, const char* game_title);
void az_nwm_start_browsing(void);
void az_nwm_connect_to_peer(const char* peer_name);
void az_nwm_send_packet(const uint8_t* data, size_t length);
void az_nwm_stop_all(void);

// Callbacks from Swift to C++
void az_nwm_hosting_started(void);
void az_nwm_peer_connected(const char* peer_name);
void az_nwm_peer_disconnected(const char* peer_name);
void az_nwm_receive_packet(const void* data, size_t length);

#ifdef __cplusplus
}
#endif
```

**File**: `src/ios/AzaharBridge/ios_bridge.mm` (ADD)

```objc
static void* g_multipeer_manager = nullptr;

void az_nwm_init_multipeer() {
    if (g_multipeer_manager == nullptr) {
        LocalPlayManager* manager = [[LocalPlayManager alloc] init];
        g_multipeer_manager = (__bridge_retained void*)manager;
    }
}

void az_nwm_start_hosting(const char* room_name, const char* title_id, 
                          const char* game_title) {
    if (g_multipeer_manager) {
        LocalPlayManager* mgr = (__bridge LocalPlayManager*)g_multipeer_manager;
        [mgr startHosting:[NSString stringWithUTF8String:room_name]
                   titleId:[NSString stringWithUTF8String:title_id]
                 gameTitle:[NSString stringWithUTF8String:game_title]];
    }
}

void az_nwm_start_browsing() {
    if (g_multipeer_manager) {
        LocalPlayManager* mgr = (__bridge LocalPlayManager*)g_multipeer_manager;
        [mgr startBrowsing];
    }
}

void az_nwm_send_packet(const uint8_t* data, size_t length) {
    if (g_multipeer_manager) {
        LocalPlayManager* mgr = (__bridge LocalPlayManager*)g_multipeer_manager;
        NSData* packet = [NSData dataWithBytes:data length:length];
        [mgr sendPacket:packet];
    }
}

void az_nwm_stop_all() {
    if (g_multipeer_manager) {
        LocalPlayManager* mgr = (__bridge LocalPlayManager*)g_multipeer_manager;
        [mgr stopAll];
    }
}

// Callbacks (implemented in NWM service)
void az_nwm_hosting_started() {
    // Trigger NWM service event
}

void az_nwm_peer_connected(const char* peer_name) {
    // Add peer to NWM service
}

void az_nwm_peer_disconnected(const char* peer_name) {
    // Remove peer from NWM service
}

void az_nwm_receive_packet(const void* data, size_t length) {
    // Queue packet in NWM service for game to pull
}
```

---

## Part 4: Implementation Roadmap

### Phase 1: Fix NWM Errors (IMMEDIATE - Today)

**Tasks**:
1. Implement `Initialize` stub with proper state flags
2. Implement `SetProbeResponseParam` stub
3. Implement `BeginHostingNetwork` stub
4. Implement `ConnectToNetwork` stub returning graceful error
5. Add warning logs instead of errors

**Files to Modify**:
- `src/core/hle/service/nwm/nwm_uds.cpp`
- `src/core/hle/service/nwm/nwm_uds.h`

**Expected Result**:
- ✅ Games don't crash when accessing local wireless
- ✅ User sees "Local wireless not supported" message
- ✅ Clean logs without scary errors

### Phase 2: Fix ExternalSceneDelegate Build (IMMEDIATE - 30 min)

**Problem**: Files created but not in Xcode project sources

**Solution**: 
1. Open Xcode project
2. Add `ExternalSceneDelegate.swift` to project
3. Add `SceneDelegate.swift` to project
4. Ensure `Info.plist` has scene configuration (already done ✅)
5. Clean build and test

**Expected Result**:
- ✅ AirPlay/HDMI automatically detected
- ✅ External display window created by iOS
- ✅ No more manual initialization needed

### Phase 3: iOS Multipeer Integration (1 week)

**Tasks**:
1. Create `LocalPlayManager.swift`
2. Add C++ bridge functions
3. Create Local Play UI in settings
4. Test 2-device connection
5. Test packet exchange

**Expected Result**:
- ✅ Basic local play between 2 iPhones
- ✅ Device discovery works
- ✅ Packet exchange functional

### Phase 4: Full NWM Implementation (2-4 weeks)

**Tasks**:
1. Implement all UDS protocol commands
2. Network synchronization
3. Error handling and recovery
4. Game-specific testing (Pokemon, Monster Hunter)
5. Performance optimization

**Expected Result**:
- ✅ Full local play support
- ✅ Pokemon trading works
- ✅ Monster Hunter multiplayer works
- ✅ Stable, reliable connections

---

## Conclusion

### Summary:

1. **NWM Errors**: Caused by incomplete stubs. Games crash because functions return unimplemented. **Fix**: Proper stubbing with state management and graceful errors.

2. **Local Play**: Possible via iOS MultipeerConnectivity. Real 802.11 impossible due to iOS restrictions. **Approach**: TCP/IP tunneling over WiFi/Bluetooth.

3. **ExternalSceneDelegate**: **CRITICAL** - Files created but not in Xcode project! Must add to build system for AirPlay to work.

### Immediate Actions Required:

**Today (30 minutes)**:
1. ✅ Add ExternalSceneDelegate.swift to Xcode project
2. ✅ Add SceneDelegate.swift to Xcode project
3. ✅ Test AirPlay detection

**This Week (2-3 hours)**:
1. ✅ Implement NWM stubs to prevent crashes
2. ✅ Add proper error logging
3. ✅ Test with games that use local wireless

**Future (When ready for local play)**:
1. Implement LocalPlayManager.swift
2. Add C++ bridge
3. Create local play UI
4. Test with 2 devices

### References:

- **Panda3DS**: `ref/panda3DS/src/core/services/nwm_uds.cpp`
- **Mikage-dev**: `ref/mikage-dev/source/processes/nwm.cpp`
- **NintendoClientsWiki**: `ref/NintendoClientsWiki/Local-Protocol.md`
- **NintendoClientsWiki**: `ref/NintendoClientsWiki/Local-Wireless-Communication-on-PC.md`

---

## Appendix: Error Code Mappings

### NWM Result Codes:
- `0x00000000` = SUCCESS
- `0xC8A1XXXX` = UDS errors
- `0xC8A10803` = Not initialized
- `0xC8A10806` = Network not found
- `0xC8A10807` = Connection refused

### iOS MultipeerConnectivity States:
- `MCSessionState.notConnected` = 0
- `MCSessionState.connecting` = 1
- `MCSessionState.connected` = 2

---

**Document End**
