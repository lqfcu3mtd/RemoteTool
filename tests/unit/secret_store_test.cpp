// SecretStore tests (CODING_STANDARDS.md section 4, PROTOCOL_SPEC.md section
// 14, CONFIG_SPEC.md sections 4 & 8).
//
// Validates the DevFileSecretStore dev-only plaintext implementation:
//   - protect/unprotect round-trip preserves bytes
//   - empty plaintext round-trips
//   - 32-byte long-term PSK round-trips (PROTOCOL_SPEC.md section 14)
//   - unprotect rejects tampered / wrong-kind / wrong-magic / truncated blob
//   - protect with len==0 is well-defined
//
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
#include <cstdint>
#include <string>
#include <vector>

#include "rmt/security/secret_store.h"
#include "rmt/test.h"

using namespace rmt::security;

namespace {

bool eq(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    return a == b;
}

void run_secret_store_tests() {
    DevFileSecretStore store;

    // --- Round-trip: small plaintext ---
    {
        const std::vector<std::uint8_t> in = {0x01, 0x02, 0x03, 0x04, 0x05};
        auto pr = store.protect(in.data(), in.size());
        const auto* prot = try_get_protected(pr);
        RMT_CHECK(prot != nullptr);
        RMT_CHECK(prot->store_kind == "devfile");
        RMT_CHECK(prot->blob.size() == in.size() + 4);
        // magic prefix
        RMT_CHECK(prot->blob[0] == 'D');
        RMT_CHECK(prot->blob[1] == 'E');
        RMT_CHECK(prot->blob[2] == 'V');
        RMT_CHECK(prot->blob[3] == '1');
        auto ur = store.unprotect(*prot);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt != nullptr);
        RMT_CHECK(eq(*pt, in));
    }

    // --- Round-trip: empty plaintext (protect(len=0) is well-defined) ---
    {
        const std::vector<std::uint8_t> empty;
        auto pr = store.protect(empty.data(), empty.size());
        const auto* prot = try_get_protected(pr);
        RMT_CHECK(prot != nullptr);
        RMT_CHECK(prot->store_kind == "devfile");
        RMT_CHECK(prot->blob.size() == 4);  // magic only
        RMT_CHECK(prot->blob[0] == 'D');
        RMT_CHECK(prot->blob[3] == '1');
        auto ur = store.unprotect(*prot);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt != nullptr);
        RMT_CHECK(pt->empty());
    }

    // --- Round-trip: 32-byte long-term PSK (PROTOCOL_SPEC.md section 14) ---
    {
        const std::vector<std::uint8_t> psk = {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
            0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
        auto pr = store.protect(psk.data(), psk.size());
        const auto* prot = try_get_protected(pr);
        RMT_CHECK(prot != nullptr);
        RMT_CHECK(prot->blob.size() == 32 + 4);
        auto ur = store.unprotect(*prot);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt != nullptr);
        RMT_CHECK(eq(*pt, psk));
    }

    // --- Round-trip: 256-byte pseudo-random pattern ---
    {
        std::vector<std::uint8_t> in;
        in.reserve(256);
        for (std::size_t i = 0; i < 256; ++i) {
            in.push_back(static_cast<std::uint8_t>(i * 7 + 3));
        }
        auto pr = store.protect(in.data(), in.size());
        const auto* prot = try_get_protected(pr);
        RMT_CHECK(prot != nullptr);
        auto ur = store.unprotect(*prot);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt != nullptr);
        RMT_CHECK(eq(*pt, in));
    }

    // --- unprotect rejects wrong store_kind (e.g. a DPAPI blob) ---
    {
        ProtectedSecret fake;
        fake.store_kind = "dpapi";  // pretend it came from a different store
        fake.blob = {'D', 'E', 'V', '1', 0x42};
        auto ur = store.unprotect(fake);
        const auto* err = try_get_unprotect_error(ur);
        RMT_CHECK(err != nullptr);
        RMT_CHECK(err->reason.find("store_kind") != std::string::npos);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt == nullptr);
    }

    // --- unprotect rejects bad magic ---
    {
        ProtectedSecret bad;
        bad.store_kind = "devfile";
        bad.blob = {'X', 'X', 'X', 'X', 0x42, 0x43};
        auto ur = store.unprotect(bad);
        const auto* err = try_get_unprotect_error(ur);
        RMT_CHECK(err != nullptr);
        RMT_CHECK(err->reason.find("magic") != std::string::npos);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt == nullptr);
    }

    // --- unprotect rejects truncated blob (shorter than magic) ---
    {
        ProtectedSecret bad;
        bad.store_kind = "devfile";
        bad.blob = {'D', 'E', 'V'};  // 3 bytes, magic is 4
        auto ur = store.unprotect(bad);
        const auto* err = try_get_unprotect_error(ur);
        RMT_CHECK(err != nullptr);
        RMT_CHECK(err->reason.find("short") != std::string::npos);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt == nullptr);
    }

    // --- unprotect rejects tampered magic byte (defense vs. wrong-store use) ---
    {
        const std::vector<std::uint8_t> in = {0xAA, 0xBB};
        auto pr = store.protect(in.data(), in.size());
        auto* prot = try_get_protected(pr);
        RMT_CHECK(prot != nullptr);
        prot->blob[0] = 'X';  // tamper the magic
        auto ur = store.unprotect(*prot);
        const auto* err = try_get_unprotect_error(ur);
        RMT_CHECK(err != nullptr);
        RMT_CHECK(err->reason.find("magic") != std::string::npos);
        const auto* pt = try_get_plaintext(ur);
        RMT_CHECK(pt == nullptr);
    }

    // --- Multiple protects produce independent blobs ---
    {
        const std::vector<std::uint8_t> a = {1, 2, 3};
        const std::vector<std::uint8_t> b = {4, 5, 6};
        auto ra = store.protect(a.data(), a.size());
        auto rb = store.protect(b.data(), b.size());
        const auto* pa = try_get_protected(ra);
        const auto* pb = try_get_protected(rb);
        RMT_CHECK(pa != nullptr);
        RMT_CHECK(pb != nullptr);
        RMT_CHECK(!eq(pa->blob, pb->blob));
        auto ua = store.unprotect(*pa);
        auto ub = store.unprotect(*pb);
        const auto* pa_pt = try_get_plaintext(ua);
        const auto* pb_pt = try_get_plaintext(ub);
        RMT_CHECK(pa_pt != nullptr);
        RMT_CHECK(pb_pt != nullptr);
        RMT_CHECK(eq(*pa_pt, a));
        RMT_CHECK(eq(*pb_pt, b));
    }
}

}  // namespace

int main() {
    run_secret_store_tests();
    auto& c = rmt::test::ctx();
    std::printf("secret_store_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
