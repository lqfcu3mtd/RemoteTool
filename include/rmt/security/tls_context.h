#pragma once
// TLS-PSK wrapper interface (Phase 5).
// Implementation: mbedTLS 3.6.1 wrapper in src/security/tls_mbedtls.cpp.
//
// PROTOCOL_SPEC.md section 14:
//   - TLS 1.2 PSK, cipher TLS_PSK_WITH_AES_128_GCM_SHA256
//   - PSK identity = "device:" + device_id (long-term) or "pair:" + device_id (pairing)
//   - Minimum 32 random bytes (256-bit) device key
//
// This header declares the abstract TLS context. The concrete implementation
// (mbedTLS) lives in the platform layer and is compiled with #ifdef _WIN32
// only (CODING_STANDARDS.md section 4).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace rmt::security {

class TlsPskContext {
public:
    virtual ~TlsPskContext() = default;

    // Configure the PSK identity and key. Must be called before handshake().
    // key must be >= 32 bytes (PROTOCOL_SPEC section 14).
    virtual bool set_psk(const std::string& identity,
                          const std::uint8_t* key, std::size_t key_len) = 0;

    // Perform TLS handshake over an already-connected TCP socket.
    // socket_fd: native socket handle (SOCKET on Windows, int on POSIX).
    // Returns true on success. On failure, call last_error() for details.
    virtual bool handshake(std::uintptr_t socket_fd) = 0;

    // Read decrypted data. Returns number of bytes read, or 0 on EOF/error.
    virtual std::size_t read(std::uint8_t* buf, std::size_t max_len) = 0;

    // Write encrypted data. Returns true on success.
    virtual bool write(const std::uint8_t* data, std::size_t len) = 0;

    // Close the TLS session (sends close_notify).
    virtual void close() = 0;

    // Human-readable last error.
    virtual std::string last_error() const = 0;
};

// Factory: creates a platform-specific TLS context.
// Returns nullptr on platforms where TLS is not available.
std::unique_ptr<TlsPskContext> create_tls_psk_context();

}  // namespace rmt::security
