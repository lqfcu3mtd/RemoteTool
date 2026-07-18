#pragma once
// SecretStore abstraction for protecting long-term secrets at rest
// (CODING_STANDARDS.md section 4; CONFIG_SPEC.md section 4 device_key_dpapi
// and section 8 agent.json device_key_dpapi; PROTOCOL_SPEC.md section 14
// long-term device key).
//
// The SecretStore interface is implemented by:
//   - DevFileSecretStore (declared below): plaintext dev-only implementation
//     for unit tests on systems without DPAPI. NOT FOR PRODUCTION.
//   - DpapiSecretStore (future, rmt::platform, Phase 4+): production DPAPI
//     implementation under include/rmt/platform/ and src/platform/. The core
//     layer (DeviceRepository, etc.) only depends on the SecretStore abstract
//     interface defined here; DPAPI calls stay strictly in the platform layer
//     (CODING_STANDARDS.md section 4: `#ifdef _WIN32` only in platform/).
//
// Result type: std::variant<T, SecretError>, mirroring the JsonParseResult
// approach in rmt/config/strict_json.h. No exceptions cross the public API
// (CODING_STANDARDS.md section 5).
//
// The interface uses (const std::uint8_t*, std::size_t) rather than
// std::span because the project targets C++17 (CODING_STANDARDS.md section
// 2.1); std::span is C++20.
//
// Logging note (CODING_STANDARDS.md section 8): SecretError::reason MUST NOT
// contain secret bytes. Implementations must not log plaintext.

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace rmt::security {

// Error returned by SecretStore operations. `reason` is a human-readable
// diagnostic that MUST NOT contain secret bytes (CODING_STANDARDS.md section
// 8: no PSK / DPAPI plaintext in logs or errors).
struct SecretError {
    std::string reason;
};

// Protected secret container. Produced by protect(), consumed by unprotect().
//
// `blob` holds the protected bytes:
//   - DpapiSecretStore: raw DPAPI blob bytes
//   - DevFileSecretStore: 4-byte ASCII magic "DEV1" + plaintext
//
// `store_kind` identifies the producing implementation ("dpapi" / "devfile")
// for diagnostics and so unprotect() can reject a ProtectedSecret produced by
// a different implementation. Callers serialize `blob` via Base64 before
// writing it to config files (CONFIG_SPEC.md section 4 device_key_dpapi);
// that encoding is handled by the config layer, not here.
struct ProtectedSecret {
    std::vector<std::uint8_t> blob;
    std::string store_kind;
};

// Result aliases. variant-based, matches rmt::config::JsonParseResult style
// (no generic Result<T,E> template exists in rmt::common yet).
using ProtectResult = std::variant<ProtectedSecret, SecretError>;
using UnprotectResult = std::variant<std::vector<std::uint8_t>, SecretError>;

// Tagged-result helpers (mirror try_get_json_value / try_get_json_error in
// rmt/config/strict_json.h). Return nullptr if the alternative is not active.
inline ProtectedSecret* try_get_protected(ProtectResult& r) noexcept {
    return std::get_if<ProtectedSecret>(&r);
}
inline const ProtectedSecret* try_get_protected(const ProtectResult& r) noexcept {
    return std::get_if<ProtectedSecret>(&r);
}
inline SecretError* try_get_protect_error(ProtectResult& r) noexcept {
    return std::get_if<SecretError>(&r);
}
inline const SecretError* try_get_protect_error(const ProtectResult& r) noexcept {
    return std::get_if<SecretError>(&r);
}
inline std::vector<std::uint8_t>* try_get_plaintext(UnprotectResult& r) noexcept {
    return std::get_if<std::vector<std::uint8_t>>(&r);
}
inline const std::vector<std::uint8_t>* try_get_plaintext(
    const UnprotectResult& r) noexcept {
    return std::get_if<std::vector<std::uint8_t>>(&r);
}
inline SecretError* try_get_unprotect_error(UnprotectResult& r) noexcept {
    return std::get_if<SecretError>(&r);
}
inline const SecretError* try_get_unprotect_error(const UnprotectResult& r) noexcept {
    return std::get_if<SecretError>(&r);
}

// Abstract secret store. Implementations protect() plaintext into a
// ProtectedSecret and unprotect() it back. Round-trip must be lossless.
//
// Thread-safety: implementations are not required to be thread-safe; callers
// must synchronize access (matching the rest of the codebase, where shared
// state is accessed from the io_context strand only).
class SecretStore {
public:
    virtual ~SecretStore() = default;

    // Protect `len` bytes starting at `data`. `data` may be nullptr only when
    // `len == 0`. Never throws; returns SecretError on failure.
    virtual ProtectResult protect(const std::uint8_t* data, std::size_t len) = 0;

    // Reverse protect(). Returns the original plaintext bytes, or SecretError
    // if `stored` was not produced by this store (wrong store_kind, bad magic,
    // tampered blob). Never returns partial or wrong data on error.
    virtual UnprotectResult unprotect(const ProtectedSecret& stored) = 0;
};

// Dev-only plaintext SecretStore. Blob layout: 4-byte ASCII magic "DEV1"
// followed by the raw plaintext. store_kind = "devfile".
//
// WARNING: This implementation stores secrets IN PLAINTEXT. It exists solely
// for unit-testing code that depends on SecretStore on systems without DPAPI
// (e.g. CI / MinGW smoke builds). It MUST NOT be used in production binaries
// or shipping builds. Production uses DpapiSecretStore (rmt::platform, Phase
// 4+). There is no fallback / silent downgrade between implementations
// (CODING_STANDARDS.md section 5: no fake implementations, no silent
// downgrade).
//
// The magic prefix is a defense-in-depth guard: a DPAPI blob handed to
// DevFileSecretStore::unprotect (or vice versa) is reported as an error
// rather than silently misinterpreted.
class DevFileSecretStore : public SecretStore {
public:
    DevFileSecretStore() = default;
    ~DevFileSecretStore() override = default;

    DevFileSecretStore(const DevFileSecretStore&) = default;
    DevFileSecretStore& operator=(const DevFileSecretStore&) = default;
    DevFileSecretStore(DevFileSecretStore&&) noexcept = default;
    DevFileSecretStore& operator=(DevFileSecretStore&&) noexcept = default;

    ProtectResult protect(const std::uint8_t* data, std::size_t len) override;
    UnprotectResult unprotect(const ProtectedSecret& stored) override;
};

}  // namespace rmt::security
