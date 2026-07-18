#include "rmt/security/secret_store.h"

#include <array>
#include <cstring>
#include <string>

namespace rmt::security {
namespace {

// Fixed 4-byte ASCII magic prefix that marks a DevFileSecretStore blob.
// Unprotect rejects any blob whose prefix does not match, so a DPAPI blob
// handed to DevFileSecretStore (or vice versa) is reported as an error rather
// than silently misinterpreted.
constexpr std::array<std::uint8_t, 4> kDevMagic = {'D', 'E', 'V', '1'};
constexpr const char* kDevKind = "devfile";

}  // namespace

ProtectResult DevFileSecretStore::protect(const std::uint8_t* data, std::size_t len) {
    ProtectedSecret out;
    out.store_kind = kDevKind;
    out.blob.reserve(kDevMagic.size() + len);
    out.blob.assign(kDevMagic.begin(), kDevMagic.end());
    if (len > 0) {
        // `data` may be nullptr only when `len == 0`; guard the insert to
        // avoid forming an invalid pointer range (data, data + 0) from a null
        // pointer.
        out.blob.insert(out.blob.end(), data, data + len);
    }
    return out;
}

UnprotectResult DevFileSecretStore::unprotect(const ProtectedSecret& stored) {
    if (stored.store_kind != kDevKind) {
        return SecretError{"store_kind mismatch: expected 'devfile', got '" +
                           stored.store_kind + "'"};
    }
    if (stored.blob.size() < kDevMagic.size()) {
        return SecretError{"blob too short: " + std::to_string(stored.blob.size()) +
                           " bytes, need at least " +
                           std::to_string(kDevMagic.size())};
    }
    if (std::memcmp(stored.blob.data(), kDevMagic.data(), kDevMagic.size()) != 0) {
        return SecretError{
            "magic mismatch: blob was not produced by DevFileSecretStore"};
    }
    std::vector<std::uint8_t> out;
    out.assign(stored.blob.begin() + kDevMagic.size(), stored.blob.end());
    return out;
}

}  // namespace rmt::security
