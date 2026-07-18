#pragma once
// Agent-side target access whitelist (PROTOCOL_SPEC.md section 13).
//
// Enforced locally inside the Agent; RemoteTool cannot bypass it. A target
// (host, port) is allowed only when ALL of the following hold:
//   - host is an IP literal (IPv4 always; IPv6 only when policy.allow_ipv6);
//   - port is listed in policy.allowed_ports;
//   - host falls inside one of policy.allowed_cidrs;
//   - port does not equal policy.self_listener_port (loopback protection).
//
// Empty allowed_cidrs or empty allowed_ports means deny-all, never allow-all.
// Domain names are never accepted (CONFIG_SPEC.md section 8).
//
// Platform isolation (CODING_STANDARDS.md section 4): no inet_pton/inet_aton/
// getaddrinfo. IPv4 and IPv6 literals are parsed by hand here.
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace rmt::security {

// Policy loaded from agent.json's target_policy object (CONFIG_SPEC.md section 8).
struct TargetPolicy {
    std::vector<std::string> allowed_cidrs;  // CIDR strings, e.g. "192.168.0.0/16"
    std::vector<int> allowed_ports;          // 1..65535 each; duplicates ignored
    bool allow_ipv6 = false;                 // when false, IPv6 targets/CIDRs rejected
    int self_listener_port = 0;              // Agent listener port, 0 = unset
};

// Result of a single check() call. `reason` is populated when !allowed.
struct TargetCheckResult {
    bool allowed = false;
    std::string reason;
};

// Parsed, validated target whitelist. Construct via the create() factory; on
// invalid policy (bad CIDR, out-of-range port) the factory returns the error
// string instead. Invalid input is never silently ignored.
class TargetWhitelist {
public:
    // Factory: parses and validates every CIDR and port up front. On success
    // returns a ready-to-use TargetWhitelist; on failure returns a human-
    // readable error describing the first offending field.
    static std::variant<TargetWhitelist, std::string> create(TargetPolicy policy);

    const TargetPolicy& policy() const noexcept { return policy_; }

    // Returns Allowed only when host is an IP literal that satisfies every
    // policy rule. host is treated as a literal IP (never resolved as DNS).
    TargetCheckResult check(const std::string& host, std::uint16_t port) const;

private:
    struct CidrV4 {
        std::uint32_t net;   // host-order network address, already masked
        std::uint32_t mask;  // host-order mask, top `prefix` bits set
    };
    struct CidrV6 {
        std::uint8_t net[16];
        std::uint8_t mask[16];
    };

    TargetWhitelist() = default;  // accessed only by create()

    static bool parse_ipv4(const std::string& s, std::uint32_t& out);
    static bool parse_ipv6(const std::string& s, std::uint8_t out[16]);
    static bool parse_cidr_v4(const std::string& s, CidrV4& out);
    static bool parse_cidr_v6(const std::string& s, CidrV6& out);
    static void make_v4_mask(std::uint32_t& m, int prefix) noexcept;
    static void make_v6_mask(std::uint8_t m[16], int prefix) noexcept;

    TargetPolicy policy_;
    std::vector<CidrV4> cidrs_v4_;
    std::vector<CidrV6> cidrs_v6_;
    std::vector<std::uint16_t> ports_;  // deduped, validated
};

}  // namespace rmt::security
