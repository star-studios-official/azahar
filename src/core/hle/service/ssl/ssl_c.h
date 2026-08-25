// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/common_types.h"
#include "core/hle/service/service.h"

// Forward declarations to avoid pulling OpenSSL into every includer
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;
typedef struct x509_st X509;
typedef struct evp_pkey_st EVP_PKEY;

namespace Kernel {
class MappedBuffer;
} // namespace Kernel

namespace Service::SSL {

// Maximum number of certificates per root cert chain (matches 3DS hardware)
constexpr u32 MaxRootCertsPerChain = 32;
// Maximum number of SSL contexts
constexpr u32 MaxSSLContexts = 16;
// Maximum number of root cert chains
constexpr u32 MaxRootCertChains = 4;

enum class SSLOption : u32 {
    None = 0,
    VerifyPeer = 1,
    VerifyHostname = 2,
};

struct SSLContextData {
    SSL_CTX* ssl_ctx = nullptr;
    u32 root_cert_chain_id = 0;
    u32 client_cert_id = 0;
    u32 options = 0;
    bool use_default_cert = false;
};

struct SSLConnectionData {
    ::SSL* ssl = nullptr;
    u32 context_id = 0;
    int sock_fd = -1;
    bool connected = false;
    bool blocking = true;
};

struct RootCertChain {
    std::vector<X509*> certs;
    bool in_use = false;
};

struct ClientCertData {
    X509* cert = nullptr;
    EVP_PKEY* key = nullptr;
    bool in_use = false;
};

class SSL_C final : public ServiceFramework<SSL_C> {
public:
    SSL_C();
    ~SSL_C();

    // For use by HTTP service to get the OpenSSL context for a connection
    ::SSL* GetSSLForConnection(u32 conn_id) const;

private:
    void Initialize(Kernel::HLERequestContext& ctx);
    void CreateContext(Kernel::HLERequestContext& ctx);
    void CreateRootCertChain(Kernel::HLERequestContext& ctx);
    void DestroyRootCertChain(Kernel::HLERequestContext& ctx);
    void AddTrustedRootCA(Kernel::HLERequestContext& ctx);
    void RootCertChainAddDefaultCert(Kernel::HLERequestContext& ctx);
    void RootCertChainRemoveCert(Kernel::HLERequestContext& ctx);
    void OpenClientCertContext(Kernel::HLERequestContext& ctx);
    void OpenDefaultClientCertContext(Kernel::HLERequestContext& ctx);
    void CloseClientCertContext(Kernel::HLERequestContext& ctx);
    void GenerateRandomData(Kernel::HLERequestContext& ctx);
    void InitializeConnectionSession(Kernel::HLERequestContext& ctx);
    void StartConnection(Kernel::HLERequestContext& ctx);
    void StartConnectionGetOut(Kernel::HLERequestContext& ctx);
    void Read(Kernel::HLERequestContext& ctx);
    void ReadPeek(Kernel::HLERequestContext& ctx);
    void Write(Kernel::HLERequestContext& ctx);
    void ContextSetRootCertChain(Kernel::HLERequestContext& ctx);
    void ContextSetClientCert(Kernel::HLERequestContext& ctx);
    void ContextClearOpt(Kernel::HLERequestContext& ctx);
    void ContextGetProtocolCipher(Kernel::HLERequestContext& ctx);
    void DestroyContext(Kernel::HLERequestContext& ctx);
    void ContextInitSharedmem(Kernel::HLERequestContext& ctx);

    // Internal helpers
    void LoadDefaultCertificates();
    SSL_CTX* CreateOpenSSLContext(u32 root_chain_id);
    bool AddCertToChain(u32 chain_id, const u8* cert_data, u32 cert_size);

    std::unordered_map<u32, SSLContextData> contexts;
    std::unordered_map<u32, SSLConnectionData> connections;
    std::unordered_map<u32, RootCertChain> root_cert_chains;
    std::unordered_map<u32, ClientCertData> client_certs;
    u32 next_context_id = 1;
    u32 next_connection_id = 1;
    u32 next_root_chain_id = 1;
    u32 next_client_cert_id = 1;

    bool initialized = false;
    std::mt19937 rng{std::random_device{}()};
};

void InstallInterfaces(Core::System& system);
void GenerateRandomData(std::vector<u8>& out);

} // namespace Service::SSL

BOOST_CLASS_EXPORT_KEY(Service::SSL::SSL_C)
