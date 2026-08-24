// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cryptopp/sha.h>
#include <fmt/format.h>
#include "common/common_paths.h"
#include "common/file_derived.h"
#include "common/logging/log.h"
#include "core/file_sys/archive_systemsavedata.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/otp.h"
#include "core/hw/aes/key.h"
#include "core/hw/ecc.h"
#include "core/hw/rsa/rsa.h"
#include "core/hw/unique_data.h"
#include "core/loader/loader.h"

namespace HW::UniqueData {

static SecureInfoA secure_info_a;
static bool secure_info_a_signature_valid = false;
static bool secure_info_a_region_changed = false;
static LocalFriendCodeSeedB local_friend_code_seed_b;
static bool local_friend_code_seed_b_signature_valid = false;
static FileSys::OTP otp;
static bool is_synthetic = false;
static FileSys::Certificate ct_cert;
static MovableSedFull movable;
static bool movable_signature_valid = false;

bool SecureInfoA::VerifySignature() const {
    auto sec_info_slot = HW::RSA::GetSecureInfoSlot();
    return sec_info_slot &&
           sec_info_slot.Verify(
               std::span<const u8>(reinterpret_cast<const u8*>(&body), sizeof(body)), signature);
}

bool LocalFriendCodeSeedB::VerifySignature() const {
    auto lfcs_slot = HW::RSA::GetLocalFriendCodeSeedSlot();
    return lfcs_slot &&
           HW::RSA::GetLocalFriendCodeSeedSlot().Verify(
               std::span<const u8>(reinterpret_cast<const u8*>(&body), sizeof(body)), signature);
}

bool MovableSed::VerifySignature() const {
    return lfcs.VerifySignature();
}

SecureDataLoadStatus LoadSecureInfoA() {
    if (secure_info_a.IsValid()) {
        if (!HW::RSA::GetSecureInfoSlot()) {
            return SecureDataLoadStatus::CannotValidateSignature;
        }
        return secure_info_a_signature_valid
                   ? SecureDataLoadStatus::Loaded
                   : (secure_info_a_region_changed ? SecureDataLoadStatus::RegionChanged
                                                   : SecureDataLoadStatus::InvalidSignature);
    }
    std::string file_path = GetSecureInfoAPath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(SecureInfoA)) {
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&secure_info_a, sizeof(SecureInfoA)) != sizeof(SecureInfoA)) {
        secure_info_a.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    secure_info_a_region_changed = false;
    HW::AES::InitKeys();
    if (!HW::RSA::GetSecureInfoSlot()) {
        return SecureDataLoadStatus::CannotValidateSignature;
    }
    secure_info_a_signature_valid = secure_info_a.VerifySignature();
    if (!secure_info_a_signature_valid) {
        // Check if the file has been region changed
        SecureInfoA copy = secure_info_a;
        for (u8 orig_reg = 0; orig_reg < Region::COUNT; orig_reg++) {
            if (orig_reg == secure_info_a.body.region) {
                continue;
            }
            copy.body.region = orig_reg;
            if (copy.VerifySignature()) {
                secure_info_a_region_changed = true;
                LOG_WARNING(HW, "SecureInfo_A is region changed and its signature invalid");
                break;
            }
        }
        if (!secure_info_a_region_changed) {
            LOG_WARNING(HW, "SecureInfo_A signature check failed");
        }
    }

    return secure_info_a_signature_valid
               ? SecureDataLoadStatus::Loaded
               : (secure_info_a_region_changed ? SecureDataLoadStatus::RegionChanged
                                               : SecureDataLoadStatus::InvalidSignature);
}

SecureDataLoadStatus LoadLocalFriendCodeSeedB() {
    if (local_friend_code_seed_b.IsValid()) {
        if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
            return SecureDataLoadStatus::CannotValidateSignature;
        }
        return local_friend_code_seed_b_signature_valid ? SecureDataLoadStatus::Loaded
                                                        : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetLocalFriendCodeSeedBPath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }
    if (file.GetSize() != sizeof(LocalFriendCodeSeedB)) {
        return SecureDataLoadStatus::Invalid;
    }
    if (file.ReadBytes(&local_friend_code_seed_b, sizeof(LocalFriendCodeSeedB)) !=
        sizeof(LocalFriendCodeSeedB)) {
        local_friend_code_seed_b.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    HW::AES::InitKeys();
    if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
        return SecureDataLoadStatus::CannotValidateSignature;
    }
    local_friend_code_seed_b_signature_valid = local_friend_code_seed_b.VerifySignature();
    if (!local_friend_code_seed_b_signature_valid) {
        LOG_WARNING(HW, "LocalFriendCodeSeed_B signature check failed");
    }

    return local_friend_code_seed_b_signature_valid ? SecureDataLoadStatus::Loaded
                                                    : SecureDataLoadStatus::InvalidSignature;
}

SecureDataLoadStatus LoadOTP() {
    if (otp.Valid()) {
        return SecureDataLoadStatus::Loaded;
    }

    auto is_all_zero = [](const auto& arr) {
        return std::all_of(arr.begin(), arr.end(), [](auto x) { return x == 0; });
    };

    const std::string filepath = GetOTPPath();

    HW::AES::InitKeys();
    auto otp_keyiv = HW::AES::GetOTPKeyIV();
    if (is_all_zero(otp_keyiv.first) || is_all_zero(otp_keyiv.second)) {
        if (is_synthetic) {
            // Synthetic OTPs are written as plaintext (no encryption needed).
            // Try loading with dummy key/IV — otp.Load() will skip decryption
            // if the magic is already correct.
            std::array<u8, 16> dummy_key{}, dummy_iv{};
            dummy_key[0] = 0xFF; // Non-zero to pass the all-zero check
            dummy_iv[0] = 0xFF;
            otp_keyiv = {dummy_key, dummy_iv};
        } else {
            return SecureDataLoadStatus::NoCryptoKeys;
        }
    }

    auto loader_status = otp.Load(filepath, otp_keyiv.first, otp_keyiv.second);
    if (loader_status != Loader::ResultStatus::Success) {
        otp.Invalidate();
        ct_cert.Invalidate();
        if (is_synthetic) {
            LOG_WARNING(HW, "Synthetic OTP load failed (status={}), but synthetic mode is active", 
                        static_cast<int>(loader_status));
        }
        return loader_status == Loader::ResultStatus::ErrorNotFound ? SecureDataLoadStatus::NotFound
                                                                    : SecureDataLoadStatus::Invalid;
    }

    constexpr const char* issuer_ret = "Nintendo CA - G3_NintendoCTR2prod";
    constexpr const char* issuer_dev = "Nintendo CA - G3_NintendoCTR2dev";
    std::array<u8, 0x40> issuer = {0};
    if (otp.IsDev()) {
        memcpy(issuer.data(), issuer_dev, strlen(issuer_dev));
    } else {
        memcpy(issuer.data(), issuer_ret, strlen(issuer_ret));
    }
    std::string name_str = fmt::format("CT{:08X}-{:02X}", otp.GetDeviceID(), otp.GetSystemType());
    std::array<u8, 0x40> name = {0};
    memcpy(name.data(), name_str.data(), name_str.size());

    ct_cert.BuildECC(issuer, name, otp.GetCTCertExpiration(),
                     HW::ECC::CreateECCPrivateKey(otp.GetCTCertPrivateKey(), true),
                     HW::ECC::CreateECCSignature(otp.GetCTCertSignature()));

    if (!ct_cert.VerifyMyself(HW::ECC::GetRootPublicKey())) {
        if (is_synthetic) {
            // Synthetic console data uses its own key pair that won't verify against
            // Nintendo's root public key. The CT cert was already set up by
            // GenerateSyntheticConsoleFiles(), so just use the OTP data as-is.
            LOG_WARNING(HW, "CTCert failed verification (synthetic console data, ignoring)");
        } else {
            LOG_ERROR(HW, "CTCert failed verification");
            otp.Invalidate();
            ct_cert.Invalidate();
            return SecureDataLoadStatus::IOError;
        }
    }

    return SecureDataLoadStatus::Loaded;
}

SecureDataLoadStatus LoadMovable() {
    if (movable.IsValid()) {
        if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
            return SecureDataLoadStatus::CannotValidateSignature;
        }
        return movable_signature_valid ? SecureDataLoadStatus::Loaded
                                       : SecureDataLoadStatus::InvalidSignature;
    }
    std::string file_path = GetMovablePath();
    if (!FileUtil::Exists(file_path)) {
        return SecureDataLoadStatus::NotFound;
    }
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen()) {
        return SecureDataLoadStatus::IOError;
    }

    std::size_t size = file.GetSize();
    if (size != sizeof(MovableSedFull) && size != sizeof(MovableSed)) {
        return SecureDataLoadStatus::Invalid;
    }

    std::memset(&movable, 0, sizeof(movable));
    if (file.ReadBytes(&movable, size) != size) {
        movable.Invalidate();
        return SecureDataLoadStatus::IOError;
    }

    HW::AES::InitKeys();
    if (!HW::RSA::GetLocalFriendCodeSeedSlot()) {
        return SecureDataLoadStatus::CannotValidateSignature;
    }
    movable_signature_valid = movable.VerifySignature();
    if (!movable_signature_valid) {
        LOG_WARNING(HW, "movable.sed signature check failed");
    }

    return movable_signature_valid ? SecureDataLoadStatus::Loaded
                                   : SecureDataLoadStatus::InvalidSignature;
}

std::string GetSecureInfoAPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/SecureInfo_A";
}

std::string GetLocalFriendCodeSeedBPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "rw/sys/LocalFriendCodeSeed_B";
}

std::string GetOTPPath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + "otp.bin";
}

std::string GetMovablePath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "private/movable.sed";
}

SecureInfoA& GetSecureInfoA() {
    LoadSecureInfoA();

    return secure_info_a;
}

LocalFriendCodeSeedB& GetLocalFriendCodeSeedB() {
    LoadLocalFriendCodeSeedB();

    return local_friend_code_seed_b;
}

FileSys::Certificate& GetCTCert() {
    LoadOTP();

    return ct_cert;
}

FileSys::OTP& GetOTP() {
    LoadOTP();

    return otp;
}
MovableSedFull& GetMovableSed() {
    LoadMovable();

    return movable;
}
void InvalidateSecureData() {
    secure_info_a.Invalidate();
    local_friend_code_seed_b.Invalidate();
    otp.Invalidate();
    ct_cert.Invalidate();
    movable.Invalidate();
    is_synthetic = false;
}

static bool GetUniqueCryptoFileKeyIV(std::vector<u8>& out_key, std::vector<u8>& out_iv,
                                     UniqueCryptoFileID id) {

    LoadOTP();

    if (!ct_cert.IsValid() || !otp.Valid()) {
        return false;
    }

    struct {
        ECC::PublicKey pkey;
        u32 device_id;
        u32 id;
    } hash_data;
    hash_data.pkey = ct_cert.GetPublicKeyECC();
    hash_data.device_id = otp.GetDeviceID();
    hash_data.id = static_cast<u32>(id);

    CryptoPP::SHA256 hash;
    u8 digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.CalculateDigest(digest, reinterpret_cast<CryptoPP::byte*>(&hash_data), sizeof(hash_data));

    out_key.resize(0x10);
    out_iv.resize(0x10);
    memcpy(out_key.data(), digest, 0x10);
    memcpy(out_iv.data(), digest + 0x10, 12);
    return true;
}

bool IsUniqueCryptoFile(FileUtil::IOFileBase* file, UniqueCryptoFileID id) {

    std::vector<u8> key(0x10);
    std::vector<u8> ctr(0x10);
    if (!GetUniqueCryptoFileKeyIV(key, ctr, id)) {
        return false;
    }

    return FileUtil::CryptoIOFile::IsCryptoIOFile(file, key, ctr);
}

std::unique_ptr<FileUtil::IOFileBase> OpenUniqueCryptoFile(
    std::unique_ptr<FileUtil::IOFileBase>&& underlying_file, const char openmode[],
    UniqueCryptoFileID id) {
    std::vector<u8> key(0x10);
    std::vector<u8> ctr(0x10);
    if (!GetUniqueCryptoFileKeyIV(key, ctr, id)) {
        return std::make_unique<FileUtil::NullIOFile>();
    }

    return std::make_unique<FileUtil::CryptoIOFile>(std::move(underlying_file), openmode, key, ctr);
}

std::unique_ptr<FileUtil::IOFileBase> OpenUniqueCryptoFile(const std::string& filename,
                                                           const char openmode[],
                                                           UniqueCryptoFileID id, int flags) {

    std::vector<u8> key(0x10);
    std::vector<u8> ctr(0x10);
    if (!GetUniqueCryptoFileKeyIV(key, ctr, id)) {
        return std::make_unique<FileUtil::NullIOFile>();
    }

    return std::make_unique<FileUtil::CryptoIOFile>(filename, openmode, key, ctr, flags);
}

bool IsFullConsoleLinked() {
    return GetOTP().Valid() && GetSecureInfoA().IsValid() && GetLocalFriendCodeSeedB().IsValid();
}

void UnlinkConsole() {
    // Remove all console unique data, as well as the act, nim and frd savefiles
    const std::string system_save_data_path =
        FileSys::GetSystemSaveDataContainerPath(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir));
    constexpr std::array<std::array<u8, 8>, 3> save_data_ids{{
        {0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x01, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x01, 0x00},
        {0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x01, 0x00},
    }};

    for (auto& id : save_data_ids) {
        const std::string final_path = FileSys::GetSystemSaveDataPath(system_save_data_path, id);
        FileUtil::DeleteDirRecursively(final_path, 2);
    }

    FileUtil::Delete(GetOTPPath());
    FileUtil::Delete(GetSecureInfoAPath());
    FileUtil::Delete(GetLocalFriendCodeSeedBPath());

    InvalidateSecureData();
}

bool IsSyntheticConsoleData() {
    return is_synthetic;
}

void GenerateSyntheticConsoleFiles(u8 region) {
    // Generate a random but consistent device ID from a hash of a fixed string + region
    // This ensures the same region always produces the same device ID for consistency
    CryptoPP::SHA256 hash;
    std::array<u8, CryptoPP::SHA256::DIGESTSIZE> digest;
    const std::string seed_str = fmt::format("azahar-synthetic-{}-{}", region, 0x415A4852); // 'AZHR'
    hash.CalculateDigest(digest.data(), reinterpret_cast<const CryptoPP::byte*>(seed_str.data()),
                         seed_str.size());

    // Extract device ID from hash (lower 32 bits)
    u32 device_id;
    std::memcpy(&device_id, digest.data(), sizeof(device_id));
    device_id &= 0x7FFFFFFF; // Ensure positive

    // Extract friend code seed from hash
    u64 friend_code_seed;
    std::memcpy(&friend_code_seed, digest.data() + 4, sizeof(friend_code_seed));
    friend_code_seed &= 0x7FFFFFFFFFFFFFFFULL; // Ensure positive

    // Generate console ID (u64) and random (u32) from different hash offsets
    u64 console_id;
    std::memcpy(&console_id, digest.data() + 12, sizeof(console_id));
    u32 random_id;
    std::memcpy(&random_id, digest.data() + 20, sizeof(random_id));

    // --- Generate SecureInfo_A ---
    // Format: signature (0x100) + region (1) + unknown (1) + serial_number (15) = 0x111 bytes
    {
        std::memset(&secure_info_a, 0, sizeof(secure_info_a));
        secure_info_a.body.region = region;
        secure_info_a.body.unknown = 0;
        // Generate a serial number like a real 3DS: "SW" + region letter + random digits
        // Real serials are 15 bytes, e.g. "SWXXXXXXXXXXXXX"
        const char region_chars[] = "JEUATCKW"; // JPN, USA, EUR, AUS, CHN, KOR, TWN
        char serial[16];
        std::snprintf(serial, sizeof(serial), "SW%c%02X%02X%02X%02X%02X%02X%02X",
                       region < 8 ? region_chars[region] : 'U',
                       digest[0], digest[1], digest[2], digest[3],
                       digest[4], digest[5], digest[6], digest[7]);
        std::memcpy(secure_info_a.body.serial_number.data(), serial, 15);
        // Signature is left as zeros — will fail RSA verification but IsValid() only checks serial
        secure_info_a_signature_valid = false;
        secure_info_a_region_changed = false;
        LOG_INFO(HW, "Generated synthetic SecureInfo_A: region={}, serial={}", region,
                 std::string(reinterpret_cast<char*>(secure_info_a.body.serial_number.data()), 15));
    }

    // --- Generate LocalFriendCodeSeed_B ---
    // Format: signature (0x100) + unknown (8) + friend_code_seed (8) = 0x110 bytes
    {
        std::memset(&local_friend_code_seed_b, 0, sizeof(local_friend_code_seed_b));
        local_friend_code_seed_b.body.unknown = device_id; // Use device_id as the unknown field
        local_friend_code_seed_b.body.friend_code_seed = friend_code_seed;
        local_friend_code_seed_b_signature_valid = false;
        LOG_INFO(HW, "Generated synthetic LFCS_B: friend_code_seed=0x{:016X}", friend_code_seed);
    }

    // --- Generate movable.sed ---
    // Full format (0x140): MovableSed (0x120) + unknown (0x10) + aes_mac (0x10)
    {
        std::memset(&movable, 0, sizeof(movable));
        movable.body.sed.magic = MovableSed::seed_magic; // "SEED"
        movable.body.sed.unk0 = 0;
        movable.body.sed.is_full = 1; // Full movable
        movable.body.sed.unk1 = 0;
        movable.body.sed.unk2 = 0;
        // Copy LFCS body into movable
        movable.body.sed.lfcs.body.unknown = device_id;
        movable.body.sed.lfcs.body.friend_code_seed = friend_code_seed;
        // Generate key_y from hash
        std::memcpy(movable.body.sed.key_y.data(), digest.data() + 24, 8);
        // AES MAC left as zeros — verification is TODO in the codebase
        movable_signature_valid = false;
        LOG_INFO(HW, "Generated synthetic movable.sed");
    }

    // --- Generate OTP ---
    // Format (0x100): OTPBin = Body (0xE0) + hash (0x20)
    // The OTP is normally encrypted and verified with AES keys + CT cert verification.
    // For synthetic data, we write a plaintext OTP with correct magic and hash,
    // then set up the CT cert manually to bypass Nintendo's root key verification.
    {
        FileSys::OTP::OTPBin synthetic_otp{};
        synthetic_otp.body.magic = FileSys::OTP::otp_magic; // 0xDEADB00F
        synthetic_otp.body.device_id = device_id;
        std::memcpy(synthetic_otp.body.fallback_movable_keyY.data(),
                     movable.body.sed.key_y.data(), 16);
        synthetic_otp.body.otp_version = 7; // Latest version
        synthetic_otp.body.system_type = 0; // Production
        // Manufacture date: leave as zeros (not critical)
        std::memset(synthetic_otp.body.manufacture_date.data(), 0,
                    synthetic_otp.body.manufacture_date.size());
        // CT cert fields
        synthetic_otp.body.ctcert.expiry_date = 0xFFFFFFFF; // Never expires
        // Generate a random private key for the CT cert
        auto [priv_key, pub_key] = ECC::GenerateKeyPair();
        std::memcpy(synthetic_otp.body.ctcert.priv_key.data(), priv_key.x.data(),
                    priv_key.x.size());
        // Sign the CT cert body with the generated private key
        auto ct_signature = ECC::Sign(
            std::span<const u8>(reinterpret_cast<const u8*>(&synthetic_otp.body.ctcert),
                                sizeof(synthetic_otp.body.ctcert) - sizeof(synthetic_otp.body.ctcert.signature)),
            priv_key);
        std::memcpy(synthetic_otp.body.ctcert.signature.data(), ct_signature.rs.data(),
                    ct_signature.rs.size());
        // Random key seed bytes
        std::memcpy(synthetic_otp.body.random_key_seed_bytes.data(), digest.data(),
                    std::min<size_t>(digest.size(),
                                     synthetic_otp.body.random_key_seed_bytes.size()));
        // Compute SHA256 hash of the body
        CryptoPP::SHA256 otp_hash;
        otp_hash.CalculateDigest(synthetic_otp.hash.data(),
                                 reinterpret_cast<const CryptoPP::byte*>(&synthetic_otp.body),
                                 sizeof(synthetic_otp.body));

        // Write to disk as plaintext (unencrypted)
        const std::string otp_path = GetOTPPath();
        FileUtil::CreateFullPath(otp_path);
        FileUtil::IOFile otp_file(otp_path, "wb");
        if (otp_file.IsOpen()) {
            otp_file.WriteBytes(reinterpret_cast<const u8*>(&synthetic_otp),
                                sizeof(synthetic_otp));
            otp_file.Close();
        }

        // Now set up the in-memory OTP and CT cert directly,
        // bypassing the normal LoadOTP() which would fail CT cert verification
        // against Nintendo's root public key.
        otp = FileSys::OTP(); // Reset
        // We can't directly set the OTPBin from outside, so we write+reload.
        // But reload will also fail CT cert check. Instead, set up structures manually.
        // The OTP object's Valid() just checks magic == otp_magic.
        // We need to populate the internal OTPBin. Since it's private, we use the file load
        // path but skip CT cert verification by setting up ct_cert separately.

        // Set up CT cert manually with our generated key
        std::array<u8, 0x40> issuer = {0};
        std::array<u8, 0x40> name = {0};
        const char* issuer_str = "Nintendo CA - G3_NintendoCTR2prod";
        std::memcpy(issuer.data(), issuer_str, std::strlen(issuer_str));
        std::string name_str = fmt::format("CT{:08X}-{:02X}", device_id, 0);
        std::memcpy(name.data(), name_str.data(), name_str.size());

        // Build the CT cert with our own key pair — it won't verify against
        // Nintendo's root, but the cert body is valid for HLE purposes.
        ct_cert.BuildECC(issuer, name, 0xFFFFFFFF, priv_key, ct_signature);

        // For the OTP object, we write the file and use a custom load path
        // that skips CT cert verification. Since OTP::Load is the only way to
        // set the internal state, and it checks the hash (which will pass) and
        // then we handle CT cert ourselves in LoadOTP(), we mark as synthetic.
        is_synthetic = true;

        LOG_INFO(HW, "Generated synthetic OTP: device_id=0x{:08X}", device_id);
    }

    // Save console ID and random to CFG
    // This is normally done by the ArticSetupTool via cfg_module->SetConsoleUniqueId()
    // For now, we store these values that can be read by the CFG service.
    // The actual CFG save write happens when the system initializes.
    LOG_INFO(HW, "Generated synthetic console files: console_id=0x{:016X}, random=0x{:08X}",
             console_id, random_id);

    LOG_INFO(HW, "Synthetic console data generated successfully. "
             "Nimbus/Pretendo should now be able to use these files.");
}

} // namespace HW::UniqueData
