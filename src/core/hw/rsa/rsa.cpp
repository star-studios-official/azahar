// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <sstream>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <cryptopp/hex.h>
#include <cryptopp/integer.h>
#include <cryptopp/nbtheory.h>
#include <cryptopp/sha.h>
#include <fmt/format.h>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/hw/aes/key.h"
#include "core/hw/rsa/rsa.h"
#include "cryptopp/osrng.h"
#include "cryptopp/rsa.h"

namespace HW::RSA {

constexpr std::size_t SlotSize = 4;
std::array<RsaSlot, SlotSize> rsa_slots;

RsaSlot ticket_wrap_slot;
RsaSlot secure_info_slot;
RsaSlot local_friend_code_seed_slot;

std::vector<u8> RsaSlot::ModularExponentiation(std::span<const u8> message,
                                               int out_size_bytes) const {
    CryptoPP::Integer sig =
        CryptoPP::ModularExponentiation(CryptoPP::Integer(message.data(), message.size()),
                                        CryptoPP::Integer(exponent.data(), exponent.size()),
                                        CryptoPP::Integer(modulus.data(), modulus.size()));

    std::vector<u8> result((out_size_bytes == -1) ? sig.MinEncodedSize() : out_size_bytes);
    sig.Encode(result.data(), result.size());
    return result;
}

std::vector<u8> RsaSlot::Sign(std::span<const u8> message) const {
    if (private_d.empty()) {
        LOG_ERROR(HW, "Cannot sign, RSA slot does not have a private key");
        return {};
    }

    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::PrivateKey private_key;
    private_key.Initialize(CryptoPP::Integer(modulus.data(), modulus.size()),
                           CryptoPP::Integer(exponent.data(), exponent.size()),
                           CryptoPP::Integer(private_d.data(), private_d.size()));

    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::Signer signer(private_key);
    CryptoPP::AutoSeededRandomPool prng;
    std::vector<u8> ret(signer.SignatureLength());

    signer.SignMessage(prng, message.data(), message.size(), ret.data());

    return ret;
}

bool RsaSlot::Verify(std::span<const u8> message, std::span<const u8> signature) const {
    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::PublicKey public_key;
    public_key.Initialize(CryptoPP::Integer(modulus.data(), modulus.size()),
                          CryptoPP::Integer(exponent.data(), exponent.size()));

    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::Verifier verifier(public_key);

    return verifier.VerifyMessage(message.data(), message.size(), signature.data(),
                                  signature.size());
}

std::vector<u8> HexToVector(const std::string& hex) {
    std::vector<u8> vector(hex.size() / 2);
    for (std::size_t i = 0; i < vector.size(); ++i) {
        vector[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }

    return vector;
}

std::optional<std::pair<std::size_t, char>> ParseKeySlotName(const std::string& full_name) {
    std::size_t slot;
    char type;
    int end;
    if (std::sscanf(full_name.c_str(), "slot0x%zX%c%n", &slot, &type, &end) == 2 &&
        end == static_cast<int>(full_name.size())) {
        return std::make_pair(slot, type);
    } else {
        return std::nullopt;
    }
}

void InitSlots() {
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    // Built-in RSA public keys for SecureInfo and LFCS verification,
    // extracted from the leaked Nintendo 3DS SDK (DER files).
    // These are the retail production keys; dev keys are also available.
    // Exponent is always 0x10001 (65537) for 3DS RSA-2048 keys.
    static const std::array<u8, 3> rsa_exponent = {0x01, 0x00, 0x01};

    // SecureInfo_A retail RSA-2048 public key modulus (from leaked SDK DER)
    static const std::array<u8, 256> secure_info_mod = {
        0xB1, 0x79, 0x1A, 0x6D, 0x1E, 0xAD, 0xD4, 0x29, 0xBA, 0x89, 0xA1, 0xCD, 0x43, 0x36, 0x30, 0x17,
        0x4B, 0xC6, 0x87, 0x30, 0xC5, 0xE7, 0x05, 0x60, 0x19, 0x7B, 0x50, 0xD8, 0xC4, 0x54, 0x67, 0x10,
        0xA6, 0xE8, 0xA1, 0x01, 0xBC, 0x2C, 0xEB, 0x03, 0x76, 0xF0, 0x05, 0xC7, 0x0C, 0xE0, 0xB6, 0xD6,
        0xDF, 0xFD, 0x26, 0xDF, 0x33, 0x46, 0x8B, 0xDB, 0xB2, 0x39, 0x1E, 0x7E, 0xC0, 0x1A, 0xA1, 0xA5,
        0xA0, 0x91, 0xE8, 0x07, 0xDA, 0x37, 0x86, 0x76, 0xBA, 0x39, 0x0A, 0x25, 0x42, 0x9D, 0x59, 0x61,
        0xE1, 0x61, 0xD4, 0x04, 0x85, 0xA7, 0x4B, 0xB2, 0x01, 0x86, 0xBE, 0xB1, 0x1A, 0x35, 0x72, 0xC1,
        0xC2, 0xEA, 0x28, 0xAB, 0x7A, 0x10, 0x15, 0x32, 0x5C, 0x9E, 0x71, 0x2B, 0x7D, 0xF9, 0x65, 0xEA,
        0xE6, 0xC6, 0xFB, 0x8B, 0xAE, 0xD7, 0x6C, 0x2A, 0x94, 0xA6, 0xC5, 0xEC, 0xE4, 0x0E, 0xAF, 0x98,
        0x7E, 0x06, 0xF2, 0x0F, 0x88, 0x4F, 0xD2, 0x06, 0x35, 0xA4, 0x76, 0xE9, 0xF7, 0x0A, 0xBA, 0x5C,
        0x5B, 0x14, 0x61, 0x52, 0x00, 0x54, 0x04, 0x45, 0x93, 0xE4, 0x68, 0x27, 0x04, 0x35, 0x35, 0x5A,
        0xAD, 0x58, 0x09, 0xD1, 0x19, 0x3F, 0x5A, 0x07, 0x28, 0xD6, 0xDB, 0x6B, 0x55, 0x1F, 0x77, 0x94,
        0x5D, 0xC3, 0xBE, 0x6F, 0xAE, 0x5B, 0xCC, 0x08, 0x63, 0xE4, 0x76, 0xDF, 0xA2, 0x9B, 0x36, 0xEA,
        0x85, 0x34, 0x03, 0xE6, 0x16, 0xEA, 0xA9, 0x05, 0xE0, 0x7F, 0x3A, 0x3E, 0x7E, 0x70, 0x77, 0xCF,
        0x16, 0x6A, 0x61, 0xD1, 0x7E, 0x4D, 0x35, 0x4C, 0x74, 0x44, 0x85, 0xD4, 0xF6, 0x7B, 0x0E, 0xEE,
        0x32, 0xF1, 0xC2, 0xD5, 0x79, 0x02, 0x48, 0xE9, 0x62, 0x1A, 0x33, 0xBA, 0xA3, 0x9B, 0x02, 0xB0,
        0x22, 0x94, 0x05, 0x7F, 0xF6, 0xB4, 0x38, 0x88, 0xE3, 0x01, 0xE5, 0x5A, 0x23, 0x7C, 0x9C, 0x0B};

    // LocalFriendCodeSeed_B retail RSA-2048 public key modulus (from leaked SDK DER)
    static const std::array<u8, 256> lfcs_mod = {
        0xA3, 0x75, 0x9A, 0x35, 0x46, 0xCF, 0xA7, 0xFE, 0x30, 0xEC, 0x55, 0xA1, 0xB6, 0x4E, 0x08, 0xE9,
        0x44, 0x9D, 0x0C, 0x72, 0xFC, 0xD1, 0x91, 0xFD, 0x61, 0x0A, 0x28, 0x89, 0x75, 0xBC, 0xE6, 0xA9,
        0xB2, 0x15, 0x56, 0xE9, 0xC7, 0x67, 0x02, 0x55, 0xAD, 0xFC, 0x3C, 0xEE, 0x5E, 0xDB, 0x78, 0x25,
        0x9A, 0x4B, 0x22, 0x1B, 0x71, 0xE7, 0xE9, 0x51, 0x5B, 0x2A, 0x67, 0x93, 0xB2, 0x18, 0x68, 0xCE,
        0x5E, 0x5E, 0x12, 0xFF, 0xD8, 0x68, 0x06, 0xAF, 0x31, 0x8D, 0x56, 0xF9, 0x54, 0x99, 0x02, 0x34,
        0x6A, 0x17, 0xE7, 0x83, 0x74, 0x96, 0xA0, 0x5A, 0xAF, 0x6E, 0xFD, 0xE6, 0xBE, 0xD6, 0x86, 0xAA,
        0xFD, 0x7A, 0x65, 0xA8, 0xEB, 0xE1, 0x1C, 0x98, 0x3A, 0x15, 0xC1, 0x7A, 0xB5, 0x40, 0xC2, 0x3D,
        0x9B, 0x7C, 0xFD, 0xD4, 0x63, 0xC5, 0xE6, 0xDE, 0xB7, 0x78, 0x24, 0xC6, 0x29, 0x47, 0x33, 0x35,
        0xB2, 0xE9, 0x37, 0xE0, 0x54, 0xEE, 0x9F, 0xA5, 0x3D, 0xD7, 0x93, 0xCA, 0x3E, 0xAE, 0x4D, 0xB6,
        0x0F, 0x5A, 0x11, 0xE7, 0x0C, 0xDF, 0xBA, 0x03, 0xB2, 0x1E, 0x2B, 0x31, 0xB6, 0x59, 0x06, 0xDB,
        0x5F, 0x94, 0x0B, 0xF7, 0x6E, 0x74, 0xCA, 0xD4, 0xAB, 0x55, 0xD9, 0x40, 0x05, 0x8F, 0x10, 0xFE,
        0x06, 0x05, 0x0C, 0x81, 0xBB, 0x42, 0x21, 0x90, 0xBA, 0x4F, 0x5C, 0x53, 0x82, 0xE1, 0xE1, 0x0F,
        0xBC, 0x94, 0x9F, 0x60, 0x69, 0x5D, 0x13, 0x03, 0xAA, 0xE2, 0xE0, 0xC1, 0x08, 0x42, 0x4C, 0x20,
        0x0B, 0x9B, 0xAA, 0x55, 0x2D, 0x55, 0x27, 0x6E, 0x24, 0xE5, 0xD6, 0x04, 0x57, 0x58, 0x8F, 0xF7,
        0x5F, 0x0C, 0xEC, 0x81, 0x9F, 0x6D, 0x2D, 0x28, 0xF3, 0x10, 0x55, 0xF8, 0x3B, 0x76, 0x62, 0xD4,
        0xE4, 0xA6, 0x93, 0x69, 0xB5, 0xDA, 0x6B, 0x40, 0x23, 0xAF, 0x07, 0xEB, 0x9C, 0xBF, 0xA9, 0xC9};

    // Set built-in defaults (will be overridden by aes_keys.txt if present)
    if (!secure_info_slot) {
        secure_info_slot.SetExponent(std::vector<u8>(rsa_exponent.begin(), rsa_exponent.end()));
        secure_info_slot.SetModulus(std::vector<u8>(secure_info_mod.begin(), secure_info_mod.end()));
        LOG_INFO(HW_RSA, "Loaded built-in SecureInfo RSA-2048 public key from leaked SDK");
    }
    if (!local_friend_code_seed_slot) {
        local_friend_code_seed_slot.SetExponent(std::vector<u8>(rsa_exponent.begin(), rsa_exponent.end()));
        local_friend_code_seed_slot.SetModulus(std::vector<u8>(lfcs_mod.begin(), lfcs_mod.end()));
        LOG_INFO(HW_RSA, "Loaded built-in LFCS RSA-2048 public key from leaked SDK");
    }

    auto s = HW::AES::GetKeysStream();

    std::string mode = "";

    while (!s.eof()) {
        std::string line;
        std::getline(s, line);

        // Ignore empty or commented lines.
        if (line.empty() || line.starts_with("#")) {
            continue;
        }

        if (line.starts_with(":")) {
            mode = line.substr(1);
            continue;
        }

        if (mode != "RSA") {
            continue;
        }

        const auto parts = Common::SplitString(line, '=');
        if (parts.size() != 2) {
            LOG_ERROR(HW_RSA, "Failed to parse {}", line);
            continue;
        }

        const std::string& name = parts[0];

        std::vector<u8> key;
        try {
            key = HexToVector(parts[1]);
        } catch (const std::logic_error& e) {
            LOG_ERROR(HW_RSA, "Invalid key {}: {}", parts[1], e.what());
            continue;
        }

        if (name == "ticketWrapExp") {
            ticket_wrap_slot.SetExponent(key);
            continue;
        }

        if (name == "ticketWrapMod") {
            ticket_wrap_slot.SetModulus(key);
            continue;
        }

        if (name == "secureInfoExp") {
            secure_info_slot.SetExponent(key);
            continue;
        }

        if (name == "secureInfoMod") {
            secure_info_slot.SetModulus(key);
            continue;
        }

        if (name == "lfcsExp") {
            local_friend_code_seed_slot.SetExponent(key);
            continue;
        }

        if (name == "lfcsMod") {
            local_friend_code_seed_slot.SetModulus(key);
            continue;
        }

        const auto key_slot = ParseKeySlotName(name);
        if (!key_slot) {
            LOG_ERROR(HW_RSA, "Invalid key name '{}'", name);
            continue;
        }

        if (key_slot->first >= SlotSize) {
            LOG_ERROR(HW_RSA, "Out of range key slot ID {:#X}", key_slot->first);
            continue;
        }

        switch (key_slot->second) {
        case 'X':
            rsa_slots.at(key_slot->first).SetExponent(key);
            break;
        case 'M':
            rsa_slots.at(key_slot->first).SetModulus(key);
            break;
        case 'P':
            rsa_slots.at(key_slot->first).SetPrivateD(key);
            break;
        default:
            LOG_ERROR(HW_RSA, "Invalid key type '{}'", key_slot->second);
            break;
        }
    }
}

static RsaSlot empty_slot;
const RsaSlot& GetSlot(std::size_t slot_id) {
    if (slot_id >= rsa_slots.size())
        return empty_slot;
    return rsa_slots[slot_id];
}

const RsaSlot& GetTicketWrapSlot() {
    return ticket_wrap_slot;
}

const RsaSlot& GetSecureInfoSlot() {
    return secure_info_slot;
}

const RsaSlot& GetLocalFriendCodeSeedSlot() {
    return local_friend_code_seed_slot;
}

} // namespace HW::RSA
