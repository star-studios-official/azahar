// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

import MultipeerConnectivity
import Foundation

/// Local Play Manager - Handles NWM::UDS emulation via iOS MultipeerConnectivity
/// Enables local wireless multiplayer between 2 or more iPhones over WiFi/Bluetooth
@objc(LocalPlayManager)
class LocalPlayManager: NSObject, ObservableObject {
    static let shared = LocalPlayManager()
    
    @Published var isHosting = false
    @Published var isConnected = false
    @Published var connectedPeers: [String] = []
    @Published var availableRooms: [(peerID: MCPeerID, info: [String: String])] = []
    
    private var session: MCSession?
    private var advertiser: MCNearbyServiceAdvertiser?
    private var browser: MCNearbyServiceBrowser?
    private let myPeerID: MCPeerID
    private let serviceType = "azahar-3ds"
    
    // Packet queue for received data
    private var receivedPackets: [Data] = []
    private let packetQueue = DispatchQueue(label: "com.azahar.localplay.packets")
    
    override init() {
        // Create peer ID from device name
        myPeerID = MCPeerID(displayName: UIDevice.current.name)
        super.init()
        
        AppLogger.info("[LocalPlay] LocalPlayManager initialized with peer: \(myPeerID.displayName)")
    }
    
    // MARK: - Host (Create Local Wireless Network)
    
    @objc func startHosting(roomName: String, titleId: String, gameTitle: String) {
        stopAll() // Clean up previous sessions
        
        // Create session with no encryption (for performance)
        session = MCSession(peer: myPeerID, securityIdentity: nil, encryptionPreference: .none)
        session?.delegate = self
        
        // Advertise with game info for discovery
        let discoveryInfo: [String: String] = [
            "roomName": roomName,
            "titleId": titleId,
            "gameTitle": gameTitle,
            "version": "1.0"
        ]
        
        advertiser = MCNearbyServiceAdvertiser(
            peer: myPeerID,
            discoveryInfo: discoveryInfo,
            serviceType: serviceType
        )
        advertiser?.delegate = self
        advertiser?.startAdvertisingPeer()
        
        isHosting = true
        
        AppLogger.info("[LocalPlay] Started hosting room: \(roomName) for titleId: \(titleId)")
        
        // Notify C++ bridge that hosting started
        az_nwm_hosting_started()
    }
    
    // MARK: - Client (Join Local Wireless Network)
    
    @objc func startBrowsing() {
        stopAll()
        
        // Create session
        session = MCSession(peer: myPeerID, securityIdentity: nil, encryptionPreference: .none)
        session?.delegate = self
        
        // Start browsing for peers
        browser = MCNearbyServiceBrowser(peer: myPeerID, serviceType: serviceType)
        browser?.delegate = self
        browser?.startBrowsingForPeers()
        
        AppLogger.info("[LocalPlay] Started browsing for local wireless sessions")
    }
    
    @objc func connectToPeer(_ peerID: MCPeerID) {
        guard let browser = browser, let session = session else {
            AppLogger.warning("LocalPlay", message: "Cannot connect - browser or session not initialized")
            return
        }
        
        browser.invitePeer(peerID, to: session, withContext: nil, timeout: 10)
        
        AppLogger.info("[LocalPlay] Inviting peer: \(peerID.displayName)")
    }
    
    // MARK: - Send/Receive Packets
    
    @objc func sendPacket(data: Data) {
        guard let session = session, !session.connectedPeers.isEmpty else {
            AppLogger.warning("LocalPlay", message: "No connected peers to send packet")
            return
        }
        
        do {
            try session.send(data, toPeers: session.connectedPeers, with: .reliable)
            AppLogger.debug("[LocalPlay] Sent packet: \(data.count) bytes to \(session.connectedPeers.count) peer(s)")
        } catch {
            AppLogger.error("LocalPlay", message: "Failed to send packet: \(error.localizedDescription)")
        }
    }
    
    @objc func receivePacket() -> Data? {
        return packetQueue.sync {
            guard !receivedPackets.isEmpty else { return nil }
            return receivedPackets.removeFirst()
        }
    }
    
    @objc func hasReceivedPackets() -> Bool {
        return packetQueue.sync {
            !receivedPackets.isEmpty
        }
    }
    
    @objc func stopAll() {
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
        
        packetQueue.sync {
            receivedPackets.removeAll()
        }
        
        AppLogger.info("[LocalPlay] Stopped all local play sessions")
    }
}

// MARK: - MCSessionDelegate

extension LocalPlayManager: MCSessionDelegate {
    func session(_ session: MCSession, peer peerID: MCPeerID, didChange state: MCSessionState) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            
            switch state {
            case .connected:
                self.connectedPeers.append(peerID.displayName)
                self.isConnected = true
                AppLogger.info("[LocalPlay] ✅ Peer connected: \(peerID.displayName)")
                
                // Notify C++ bridge
                az_nwm_peer_connected(peerID.displayName)
                
            case .connecting:
                AppLogger.info("[LocalPlay] 🔄 Connecting to peer: \(peerID.displayName)")
                
            case .notConnected:
                self.connectedPeers.removeAll { $0 == peerID.displayName }
                if self.connectedPeers.isEmpty {
                    self.isConnected = false
                }
                AppLogger.info("[LocalPlay] ❌ Peer disconnected: \(peerID.displayName)")
                
                // Notify C++ bridge
                az_nwm_peer_disconnected(peerID.displayName)
                
            @unknown default:
                AppLogger.warning("LocalPlay", message: "Unknown session state: \(state.rawValue)")
            }
        }
    }
    
    func session(_ session: MCSession, didReceive data: Data, fromPeer peerID: MCPeerID) {
        // Queue received packet
        packetQueue.sync {
            receivedPackets.append(data)
        }
        
        // Notify C++ bridge that packet is available
        data.withUnsafeBytes { bytes in
            if let baseAddress = bytes.baseAddress {
                az_nwm_receive_packet(baseAddress, data.count)
            }
        }
        
        AppLogger.debug("[LocalPlay] 📥 Received packet: \(data.count) bytes from \(peerID.displayName)")
    }
    
    func session(_ session: MCSession, didReceive stream: InputStream, withName streamName: String, fromPeer peerID: MCPeerID) {
        // Streams not used for NWM tunneling
    }
    
    func session(_ session: MCSession, didStartReceivingResourceWithName resourceName: String, fromPeer peerID: MCPeerID, with progress: Progress) {
        // Resources not used for NWM tunneling
    }
    
    func session(_ session: MCSession, didFinishReceivingResourceWithName resourceName: String, fromPeer peerID: MCPeerID, at localURL: URL?, withError error: Error?) {
        // Resources not used for NWM tunneling
    }
}

// MARK: - MCNearbyServiceAdvertiserDelegate

extension LocalPlayManager: MCNearbyServiceAdvertiserDelegate {
    func advertiser(_ advertiser: MCNearbyServiceAdvertiser, didReceiveInvitationFromPeer peerID: MCPeerID, withContext context: Data?, invitationHandler: @escaping (Bool, MCSession?) -> Void) {
        // Auto-accept invitations (host allows anyone to join)
        AppLogger.info("[LocalPlay] 📨 Received invitation from: \(peerID.displayName)")
        invitationHandler(true, session)
    }
    
    func advertiser(_ advertiser: MCNearbyServiceAdvertiser, didNotStartAdvertisingPeer error: Error) {
        AppLogger.error("LocalPlay", message: "Failed to start advertising: \(error.localizedDescription)")
    }
}

// MARK: - MCNearbyServiceBrowserDelegate

extension LocalPlayManager: MCNearbyServiceBrowserDelegate {
    func browser(_ browser: MCNearbyServiceBrowser, foundPeer peerID: MCPeerID, withDiscoveryInfo info: [String : String]?) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            
            let roomName = info?["roomName"] ?? "Unknown Room"
            let titleId = info?["titleId"] ?? "Unknown"
            let gameTitle = info?["gameTitle"] ?? "Unknown Game"
            
            // Add to available rooms
            self.availableRooms.append((peerID, info ?? [:]))
            
            AppLogger.info("[LocalPlay] 🔍 Found peer: \(peerID.displayName)")
            AppLogger.info("[LocalPlay]    Room: \(roomName) | Game: \(gameTitle) | TitleID: \(titleId)")
            
            // Notify UI about available room (could trigger a notification)
        }
    }
    
    func browser(_ browser: MCNearbyServiceBrowser, lostPeer peerID: MCPeerID) {
        DispatchQueue.main.async { [weak self] in
            guard let self = self else { return }
            
            self.availableRooms.removeAll { $0.peerID == peerID }
            AppLogger.info("[LocalPlay] 📤 Lost peer: \(peerID.displayName)")
        }
    }
    
    func browser(_ browser: MCNearbyServiceBrowser, didNotStartBrowsingForPeers error: Error) {
        AppLogger.error("LocalPlay", message: "Failed to start browsing: \(error.localizedDescription)")
    }
}

// MARK: - C++ Bridge Function Declarations

@_silgen_name("az_nwm_hosting_started")
func az_nwm_hosting_started()

@_silgen_name("az_nwm_peer_connected")
func az_nwm_peer_connected(_ peerName: UnsafePointer<CChar>)

@_silgen_name("az_nwm_peer_disconnected")
func az_nwm_peer_disconnected(_ peerName: UnsafePointer<CChar>)

@_silgen_name("az_nwm_receive_packet")
func az_nwm_receive_packet(_ data: UnsafeRawPointer, _ length: Int)
