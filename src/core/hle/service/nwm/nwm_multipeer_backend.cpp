// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hle/service/nwm/nwm_multipeer_backend.h"

#ifdef CITRA_IOS

#include "common/logging/log.h"
#include "core/hle/service/nwm/nwm_uds.h"

// Forward declarations for iOS bridge functions
extern "C" {
void az_nwm_init_multipeer(void);
void az_nwm_start_hosting(const char* room_name, const char* title_id, const char* game_title);
void az_nwm_start_browsing(void);
void az_nwm_connect_to_peer(const char* peer_name);
void az_nwm_send_packet(const uint8_t* data, size_t length);
void az_nwm_stop_all(void);
bool az_nwm_has_received_packets(void);
void* az_nwm_pull_packet(void);
}

namespace Service::NWM {

NWMMultipeerBackend::NWMMultipeerBackend() {
    LOG_INFO(Service_NWM, "[Multipeer] Backend initialized");
}

NWMMultipeerBackend::~NWMMultipeerBackend() {
    StopAll();
}

bool NWMMultipeerBackend::Initialize() {
    if (is_initialized) {
        return true;
    }

    az_nwm_init_multipeer();
    is_initialized = true;

    LOG_INFO(Service_NWM, "[Multipeer] Initialized iOS MultipeerConnectivity backend");
    return true;
}

bool NWMMultipeerBackend::StartHosting(const std::string& room_name, u64 title_id,
                                       const std::string& game_title) {
    if (!is_initialized) {
        Initialize();
    }

    // Convert title ID to hex string
    char title_id_str[17];
    snprintf(title_id_str, sizeof(title_id_str), "%016llX", title_id);

    az_nwm_start_hosting(room_name.c_str(), title_id_str, game_title.c_str());

    is_hosting = true;

    LOG_INFO(Service_NWM, "[Multipeer] Started hosting: {} | TitleID: {} | Game: {}", 
             room_name, title_id_str, game_title);

    return true;
}

bool NWMMultipeerBackend::StartBrowsing() {
    if (!is_initialized) {
        Initialize();
    }

    az_nwm_start_browsing();

    LOG_INFO(Service_NWM, "[Multipeer] Started browsing for local wireless sessions");
    return true;
}

bool NWMMultipeerBackend::ConnectToPeer(const std::string& peer_name) {
    if (!is_initialized) {
        return false;
    }

    az_nwm_connect_to_peer(peer_name.c_str());

    LOG_INFO(Service_NWM, "[Multipeer] Connecting to peer: {}", peer_name);
    return true;
}

bool NWMMultipeerBackend::SendPacket(const std::vector<u8>& data) {
    if (!is_initialized || data.empty()) {
        return false;
    }

    az_nwm_send_packet(data.data(), data.size());

    LOG_DEBUG(Service_NWM, "[Multipeer] Sent packet: {} bytes", data.size());
    return true;
}

bool NWMMultipeerBackend::HasReceivedPackets() const {
    if (!is_initialized) {
        return false;
    }

    return az_nwm_has_received_packets();
}

std::vector<u8> NWMMultipeerBackend::PullPacket() {
    if (!is_initialized) {
        return {};
    }

    void* packet_ptr = az_nwm_pull_packet();
    if (packet_ptr == nullptr) {
        return {};
    }

    // The returned pointer is to a std::vector<u8> in ios_bridge.mm
    std::vector<u8>* packet_vec = static_cast<std::vector<u8>*>(packet_ptr);
    std::vector<u8> result = *packet_vec;

    LOG_DEBUG(Service_NWM, "[Multipeer] Pulled packet: {} bytes", result.size());
    return result;
}

void NWMMultipeerBackend::StopAll() {
    if (!is_initialized) {
        return;
    }

    az_nwm_stop_all();

    is_hosting = false;
    is_connected = false;

    LOG_INFO(Service_NWM, "[Multipeer] Stopped all local wireless sessions");
}

} // namespace Service::NWM

#endif // CITRA_IOS
