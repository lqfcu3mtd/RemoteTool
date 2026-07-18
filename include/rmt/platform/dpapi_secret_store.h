#pragma once
// DPAPI-backed SecretStore implementation (Windows only).
// CODING_STANDARDS.md section 4: #ifdef _WIN32 only in platform/ layer.
//
// Wraps CryptProtectData / CryptUnprotectData with
// CRYPTPROTECT_UI_FORBIDDEN | CRYPTPROTECT_LOCAL_MACHINE (current-user scope
// per CONFIG_SPEC.md section 4). The DPAPI blob is stored verbatim inside
// ProtectedSecret::blob; callers serialise it to Base64 for device_key_dpapi.
#ifdef _WIN32

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "rmt/security/secret_store.h"

namespace rmt::platform {

class DpapiSecretStore : public rmt::security::SecretStore {
public:
    DpapiSecretStore() = default;
    ~DpapiSecretStore() override = default;

    DpapiSecretStore(const DpapiSecretStore&) = default;
    DpapiSecretStore& operator=(const DpapiSecretStore&) = default;
    DpapiSecretStore(DpapiSecretStore&&) noexcept = default;
    DpapiSecretStore& operator=(DpapiSecretStore&&) noexcept = default;

    rmt::security::ProtectResult protect(const std::uint8_t* data,
                                         std::size_t len) override;
    rmt::security::UnprotectResult unprotect(
        const rmt::security::ProtectedSecret& stored) override;
};

}  // namespace rmt::platform

#endif  // _WIN32
