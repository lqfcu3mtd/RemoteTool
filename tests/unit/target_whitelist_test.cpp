// Target whitelist tests (PROTOCOL_SPEC.md section 13 + TEST_PLAN.md section 3.5).
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
#include <cstdint>
#include <string>
#include <variant>

#include "rmt/security/target_whitelist.h"
#include "rmt/test.h"

using namespace rmt::security;

namespace {

void run_tests() {
    // --- 1. IPv4 hit (192.168.1.5 in 192.168.0.0/16 + port 22) ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {22, 80};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("192.168.1.5", 22);
            RMT_CHECK_MSG(res.allowed, res.reason.c_str());
        }
    }

    // --- 2. IPv4 miss (10.0.0.1 not in 192.168.0.0/16) ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("10.0.0.1", 22);
            RMT_CHECK_MSG(!res.allowed, "10.0.0.1 must not match 192.168.0.0/16");
        }
    }

    // --- 3. Loopback 127.0.0.1 in 127.0.0.0/8 ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"127.0.0.0/8"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("127.0.0.1", 22);
            RMT_CHECK_MSG(res.allowed, res.reason.c_str());
        }
    }

    // --- 4. Port not in allowed_ports -> denied ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {22, 80};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("192.168.1.1", 443);
            RMT_CHECK_MSG(!res.allowed, "port 443 must be denied");
            RMT_CHECK_MSG(!res.reason.empty(), "reason must be populated on denial");
        }
    }

    // --- 5. Empty allowed_cidrs -> deny all ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("192.168.1.1", 22);
            RMT_CHECK_MSG(!res.allowed, "empty allowed_cidrs must deny all");
        }
    }

    // --- 6. Empty allowed_ports -> deny all ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("192.168.1.1", 22);
            RMT_CHECK_MSG(!res.allowed, "empty allowed_ports must deny all");
        }
    }

    // --- 7. Domain host -> denied ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"0.0.0.0/0"};
        p.allowed_ports = {22, 80, 443};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("example.com", 22);
            RMT_CHECK_MSG(!res.allowed, "domain host must be denied");
            RMT_CHECK_MSG(res.reason.find("IP literal") != std::string::npos,
                          "reason must mention IP literal");
        }
    }

    // --- 8a. allow_ipv6=false + IPv6 host -> denied ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"0.0.0.0/0"};
        p.allowed_ports = {22};
        p.allow_ipv6 = false;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("::1", 22);
            RMT_CHECK_MSG(!res.allowed, "IPv6 host with allow_ipv6=false must be denied");
            RMT_CHECK_MSG(res.reason.find("IPv6") != std::string::npos,
                          "reason must mention IPv6");
        }
    }

    // --- 8b. allow_ipv6=true + IPv6 host hit ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"2001:db8::/32"};
        p.allowed_ports = {443};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("2001:db8::1", 443);
            RMT_CHECK_MSG(res.allowed, res.reason.c_str());
        }
    }

    // --- 8c. allow_ipv6=true + IPv6 miss ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"2001:db8::/32"};
        p.allowed_ports = {443};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("2001:dead::1", 443);
            RMT_CHECK_MSG(!res.allowed, "2001:dead::1 must not match 2001:db8::/32");
        }
    }

    // --- 8d. IPv6 CIDR present but allow_ipv6=false -> create fails ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"::/0"};
        p.allowed_ports = {22};
        p.allow_ipv6 = false;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- 9. self_listener_port equals target port -> denied ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"127.0.0.0/8"};
        p.allowed_ports = {4433};
        p.self_listener_port = 4433;
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("127.0.0.1", 4433);
            RMT_CHECK_MSG(!res.allowed, "self listener port must be denied");
            RMT_CHECK_MSG(res.reason.find("loopback") != std::string::npos,
                          "reason must mention loopback");
        }
    }

    // --- 9b. self_listener_port=0 (unset) does not trigger loopback ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"127.0.0.0/8"};
        p.allowed_ports = {4433};
        p.self_listener_port = 0;
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("127.0.0.1", 4433);
            RMT_CHECK_MSG(res.allowed, res.reason.c_str());
        }
    }

    // --- 10a. Invalid CIDR: 999.0.0.0/8 ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"999.0.0.0/8"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- 10b. Invalid CIDR: 192.168.1.0/40 (prefix > 32) ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.1.0/40"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- 10c. Invalid port: 0 ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {0};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- 10d. Invalid port: 70000 ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {70000};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- 10e. Invalid IPv6 CIDR: ::1/200 (prefix > 128) ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"::1/200"};
        p.allowed_ports = {22};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- 10f. Invalid IPv6: two "::" ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"2001::db8::1/64"};
        p.allowed_ports = {22};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- Extra: 0.0.0.0/0 matches any IPv4 ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"0.0.0.0/0"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(w->check("1.2.3.4", 22).allowed);
            RMT_CHECK(w->check("255.255.255.255", 22).allowed);
            RMT_CHECK(w->check("0.0.0.0", 22).allowed);
        }
    }

    // --- Extra: /32 exact match ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"10.1.2.3/32"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(w->check("10.1.2.3", 22).allowed);
            RMT_CHECK(!w->check("10.1.2.4", 22).allowed);
        }
    }

    // --- Extra: multiple CIDRs, match any ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"10.0.0.0/8", "192.168.0.0/16"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(w->check("10.5.5.5", 22).allowed);
            RMT_CHECK(w->check("192.168.1.1", 22).allowed);
            RMT_CHECK(!w->check("172.16.0.1", 22).allowed);
        }
    }

    // --- Extra: port dedup ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.0.0/16"};
        p.allowed_ports = {22, 22, 80, 80};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(w->check("192.168.1.1", 22).allowed);
            RMT_CHECK(w->check("192.168.1.1", 80).allowed);
            RMT_CHECK(!w->check("192.168.1.1", 443).allowed);
        }
    }

    // --- Extra: IPv4-mapped IPv6 (::ffff:a.b.c.d) ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"::ffff:0:0/96"};
        p.allowed_ports = {443};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("::ffff:192.168.1.1", 443);
            RMT_CHECK_MSG(res.allowed, res.reason.c_str());
        }
    }

    // --- Extra: IPv6 full-form 8-group ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"2001:0db8:0000:0000:0000:0000:0000:0000/48"};
        p.allowed_ports = {443};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<TargetWhitelist>(r));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(w->check("2001:db8::1", 443).allowed);
            RMT_CHECK(!w->check("2001:dead::1", 443).allowed);
        }
    }

    // --- Extra: IPv6 uppercase hex ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"2001:DB8::/32"};
        p.allowed_ports = {443};
        p.allow_ipv6 = true;
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(w->check("2001:db8::1", 443).allowed);
            RMT_CHECK(w->check("2001:DB8::ABCD", 443).allowed);
        }
    }

    // --- Extra: empty host string -> denied ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"0.0.0.0/0"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("", 22);
            RMT_CHECK_MSG(!res.allowed, "empty host must be denied");
        }
    }

    // --- Extra: IPv4 with leading zeros -> rejected by parser ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"192.168.001.1/24"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        RMT_CHECK(std::holds_alternative<std::string>(r));
    }

    // --- Extra: IPv4 trailing junk -> denied at check time ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"0.0.0.0/0"};
        p.allowed_ports = {22};
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            RMT_CHECK(!w->check("1.2.3.4.5", 22).allowed);
            RMT_CHECK(!w->check("1.2.3", 22).allowed);
            RMT_CHECK(!w->check("1.2.3.256", 22).allowed);
            RMT_CHECK(!w->check("1.2.3.-1", 22).allowed);
        }
    }

    // --- Extra: loopback protection fires before IP/CIDR checks ---
    {
        TargetPolicy p;
        p.allowed_cidrs = {"10.0.0.0/8"};  // does NOT contain 192.168.x.x
        p.allowed_ports = {4433};
        p.self_listener_port = 4433;
        auto r = TargetWhitelist::create(std::move(p));
        const auto* w = std::get_if<TargetWhitelist>(&r);
        RMT_CHECK(w != nullptr);
        if (w) {
            auto res = w->check("192.168.1.1", 4433);
            RMT_CHECK_MSG(!res.allowed, "loopback must deny even if CIDR would miss");
            RMT_CHECK_MSG(res.reason.find("loopback") != std::string::npos,
                          "loopback reason must win");
        }
    }
}

}  // namespace

int main() {
    run_tests();
    auto& c = rmt::test::ctx();
    std::printf("target_whitelist_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
