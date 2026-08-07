// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include <vector>
#include <queue>

#ifdef CITRA_IOS

// iOS MultipeerConnectivity backend for NWM::UDS emulation
// This provides local wireless multiplayer between iPhones over WiFi/Bluetooth

namespace Service::NWM {

/// iOS-specific NWM backend using MultipeerConnectivity
class NWMMultipeerBackend {
public:
    NWMMultipeerBackend();
    ~NWMMultipeerBackend();

    /// Initialize the Multipeer backend
    bool Initialize();

    /// Start hosting a local wireless network
    /// @param room_name Display name for the room
    /// @param title_id Game title ID
    /// @param game_title Human-readable game title
    /// @return true if hosting started successfully
    bool StartHosting(const std::string& room_name, u64 title_id, const std::string& game_title);

    /// Start browsing for available local wireless networks
    /// @return true if browsing started successfully
    bool StartBrowsing();

    /// Connect to a specific peer
    /// @param peer_name Peer display name
    /// @return true if connection initiated
    bool ConnectToPeer(const std::string& peer_name);

    /// Send packet to all connected peers
    /// @param data Packet data
    /// @return true if packet sent successfully
    bool SendPacket(const std::vector<u8>& data);

    /// Check if there are received packets available
    bool HasReceivedPackets() const;

    /// Pull a received packet
    /// @return Packet data, or empty vector if no packets available
    std::vector<u8> PullPacket();

    /// Check if currently hosting
    bool IsHosting() const { return is_hosting; }

    /// Check if connected to peers
    bool IsConnected() const { return is_connected; }

    /// Stop all local wireless sessions
    void StopAll();

private:
    bool is_initialized = false;
    bool is_hosting = false;
    bool is_connected = false;
};

} // namespace Service::NWM

#endif // CITRA_IOS
