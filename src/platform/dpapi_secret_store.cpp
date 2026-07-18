#ifdef _WIN32
#include "rmt/platform/dpapi_secret_store.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstring>
#include <string>
#include <vector>

namespace rmt::platform {
namespace {

// Additional entropy (optional). Empty = no extra entropy.
// If we add entropy later it must be stored alongside the blob.
DATA_BLOB kEntropy = {0, nullptr};

}  // namespace

rmt::security::ProtectResult DpapiSecretStore::protect(
    const std::uint8_t* data, std::size_t len) {
    auto dpapi_err = [](const char* s) -> rmt::security::ProtectResult {
        return rmt::security::SecretError{
            std::string("DPAPI ") + s + " failed: " +
            std::to_string(::GetLastError())};
    };
    if (len == 0) {
        return rmt::security::SecretError{"DPAPI protect: empty input"};
    }

    DATA_BLOB in_blob;
    in_blob.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data));
    in_blob.cbData = static_cast<DWORD>(len);

    DATA_BLOB out_blob = {0, nullptr};
    if (!::CryptProtectData(&in_blob, nullptr, &kEntropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
        return dpapi_err("CryptProtectData");
    }

    rmt::security::ProtectedSecret sec;
    sec.store_kind = "dpapi";
    sec.blob.assign(out_blob.pbData, out_blob.pbData + out_blob.cbData);
    ::LocalFree(out_blob.pbData);
    return sec;
}

rmt::security::UnprotectResult DpapiSecretStore::unprotect(
    const rmt::security::ProtectedSecret& stored) {
    auto dpapi_err = [](const char* s) -> rmt::security::UnprotectResult {
        return rmt::security::SecretError{
            std::string("DPAPI ") + s + " failed: " +
            std::to_string(::GetLastError())};
    };
    if (stored.store_kind != "dpapi") {
        return rmt::security::SecretError{
            "store_kind mismatch: expected 'dpapi', got '" +
            stored.store_kind + "'"};
    }

    DATA_BLOB in_blob;
    in_blob.pbData =
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(stored.blob.data()));
    in_blob.cbData = static_cast<DWORD>(stored.blob.size());

    DATA_BLOB out_blob = {0, nullptr};
    LPWSTR desc = nullptr;
    if (!::CryptUnprotectData(&in_blob, &desc, &kEntropy, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
        if (desc) ::LocalFree(desc);
        return dpapi_err("CryptUnprotectData");
    }
    if (desc) ::LocalFree(desc);

    std::vector<std::uint8_t> result(out_blob.pbData,
                                     out_blob.pbData + out_blob.cbData);
    ::LocalFree(out_blob.pbData);
    return result;
}

}  // namespace rmt::platform
#endif  // _WIN32
