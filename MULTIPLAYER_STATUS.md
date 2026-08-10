# iOS Multiplayer Status Report

## Current State: MultipeerConnectivity IS WORKING ✅

### What Works
1. ✅ **MultipeerConnectivity initialization** - Both phones successfully initialize
2. ✅ **Hosting/Advertising** - Host phone broadcasts its game session
3. ✅ **Peer Discovery** - Client phone discovers host phone
4. ✅ **Permissions** - Local network and Bonjour permissions work correctly

**Evidence from logs:**
```
[  15.803090] [LocalPlay] 🔍 Found peer: iPhone
[  15.803100] [LocalPlay]    Room: Azahar_0004000000175E00 | Game: Pokémon Moon | TitleID: 0004000000175E00
```

### Critical Missing Piece: Peer Discovery → Beacon Bridge

**The Problem:**
- Swift layer discovers peers and stores them in `availableRooms` array
- NO callback exists to notify C++ layer about discovered peers
- C++ `GetReceivedBeacons()` returns empty list
- Games see no networks even though peers are discovered

**The Solution Needed:**
1. Add C++ callback function `az_nwm_peer_discovered(peer_info)` in `ios_bridge.mm`
2. Swift calls this when peers are found in `browser(_:foundPeer:withDiscoveryInfo:)`
3. C++ generates fake 802.11 beacon packet from peer info
4. Inject beacon into `received_beacons` list in NWM_UDS
5. Game now sees discovered networks

### 3DS Local Wireless Protocol (UDS)

**How 3DS games connect (from our implementation):**
1. Host calls `BeginHostingNetwork` → Creates network, starts beacon broadcast
2. Client calls `RecvBeaconBroadcastData` → Scans for 802.11 beacons
3. Client receives beacon with network info (SSID, MAC, channel, application data)
4. Client calls `ConnectToNetwork` with network info from beacon
5. Connection sequence: Authentication → Association → EAPoL → Data packets

**What we're emulating:**
- Host: Create MultipeerConnectivity advertiser (✅ working)
- Client: Create MultipeerConnectivity browser (✅ working)
- **Missing**: Convert discovered MCPeerID → fake beacon packet

## Completed Work

### 1. NWMMultipeerBackend Integration
**Files Modified:**
- `src/core/hle/service/nwm/nwm_uds.h` - Added backend member
- `src/core/hle/service/nwm/nwm_uds.cpp` - Initialize and call backend

**Changes:**
```cpp
// Constructor - initialize backend
#ifdef CITRA_IOS
    multipeer_backend = std::make_unique<NWMMultipeerBackend>();
    multipeer_backend->Initialize();
#endif

// BeginHostingNetwork - start advertising
if (multipeer_backend) {
    multipeer_backend->StartHosting(room_name, title_id, game_title);
}

// RecvBeaconBroadcastData - start browsing
if (multipeer_backend && connection_status.status == NetworkStatus::NotConnected) {
    multipeer_backend->StartBrowsing();
}
```

### 2. Settings Toggle Added
**Files Modified:**
- `CMakeModules/GenerateSettingKeys.cmake` - Added `enable_multipeer_connectivity` key

**Still Needed:**
- Default value in config_ios.cpp (set to `true` by default)
- Check setting before initializing backend
- iOS UI toggle in settings screen

## Next Steps (Priority Order)

### 1. CRITICAL: Implement Peer Discovery Bridge (Required for functionality)

**Add to `ios_bridge.mm`:**
```cpp
extern "C" void az_nwm_peer_discovered(const char* peer_name, const char* room_name, 
                                       const char* title_id_str, const char* game_title) {
    // Convert peer info to beacon packet
    // This is what the game expects to see when scanning
    
    // Inject into received_beacons list in NWM_UDS service
    // Games will then see this as a discoverable network
}
```

**Modify `LocalPlayManager.swift`:**
```swift
func browser(_ browser: MCNearbyServiceBrowser, foundPeer peerID: MCPeerID, 
             withDiscoveryInfo info: [String : String]?) {
    // Existing code...
    self.availableRooms.append((peerID, info ?? [:]))
    
    // NEW: Call C++ bridge
    az_nwm_peer_discovered(
        peerID.displayName.cString(using: .utf8),
        roomName.cString(using: .utf8),
        titleId.cString(using: .utf8),
        gameTitle.cString(using: .utf8)
    )
}
```

### 2. Complete Settings Toggle

**Add to `config_ios.cpp`:**
```cpp
void Config::ReadValues() {
    // ... existing settings ...
    
    Settings::values.enable_multipeer_connectivity = 
        ReadSetting("System", "enable_multipeer_connectivity", true);
}
```

**Update NWM_UDS constructor:**
```cpp
#ifdef CITRA_IOS
    if (Settings::values.enable_multipeer_connectivity.GetValue()) {
        multipeer_backend = std::make_unique<NWMMultipeerBackend>();
        multipeer_backend->Initialize();
    }
#endif
```

### 3. Add iOS UI Toggle

**Add to iOS settings screen** (probably in SystemSettingsView.swift):
```swift
Toggle("Local Multiplayer (MultipeerConnectivity)", 
       isOn: $enableMultipeerConnectivity)
    .onChange(of: enableMultipeerConnectivity) { value in
        // Save to config
    }
```

### 4. Generate Proper Beacon Packets

**Key fields needed in beacon:**
- MAC address (can generate random one per peer)
- Network channel (default 11)
- Network ID (from title ID)
- Application data (200 bytes from game)
- Host node info (username, etc.)

**Reference:** See `HandleBeaconFrame()` in `nwm_uds.cpp` for expected beacon format

## Known Issues

### Not a Priority (Working as designed):
- Both phones try to host AND browse simultaneously - This is correct! The game UI determines whether you're host or client, not the emulator.

### Potential Issues:
1. **Beacon format** - May need to match exact 802.11 beacon structure
2. **Application data** - Games put custom data in beacons (room settings, etc.) - We don't have this from peers
3. **Connection sequence** - After beacon discovery, actual connection uses different path (may need more work)

## Testing Checklist

Once peer discovery bridge is implemented:
- [ ] Phone 1 hosts game (Pokemon Moon)
- [ ] Phone 2 scans for games
- [ ] Phone 2 sees "Pokemon Moon" in list
- [ ] Phone 2 attempts to connect
- [ ] Monitor logs for connection sequence
- [ ] Verify data packets flow between devices

## Files Reference

**Core Multiplayer:**
- `src/core/hle/service/nwm/nwm_uds.cpp` - Main UDS service
- `src/core/hle/service/nwm/nwm_multipeer_backend.cpp` - iOS backend

**iOS Bridge:**
- `src/ios/AzaharBridge/ios_bridge.mm` - C++/ObjC bridge
- `src/ios/AzaharApp/Utilities/LocalPlayManager.swift` - MultipeerConnectivity manager

**Settings:**
- `CMakeModules/GenerateSettingKeys.cmake` - Setting key definitions
- `src/ios/AzaharBridge/config_ios.cpp` - iOS config reader

## Logs Analysis

**Phone 1 (Host - Pokemon Moon):**
- Backend initialized ✅
- Hosting started ✅
- Browsing started (unnecessary but harmless) ✅
- NO peer discoveries (correct - it's the host)

**Phone 2 (Client - Pokemon Ultra Moon):**
- Backend initialized ✅
- Hosting started (game started as host first) ✅
- Browsing started ✅
- **PEER DISCOVERED** ✅ - "Azahar_0004000000175E00 | Pokémon Moon"
- **BUT:** Game doesn't see it (no beacon callback)

## Summary

**Good news:** MultipeerConnectivity is 100% functional. Peer discovery works perfectly.

**Missing:** One bridge function to convert Swift peer discoveries into C++ beacon packets that games expect.

**Estimated work:** 2-3 hours to implement peer discovery bridge and test end-to-end connection flow.

---

*Last updated: 2026-08-10*
*Build: 28329126b2271977fa90a53755aa24aaa90b2cf5*
