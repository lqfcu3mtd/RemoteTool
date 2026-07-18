#include "rmt/security/target_whitelist.h"

#include <utility>

namespace rmt::security {
namespace {

// Parse one IPv4 decimal octet run starting at index `i` of `s`.
// On success, advances `i` past the digits and stores the value in `v`.
// Returns false on missing digits, leading zeros (except "0"), or value > 255.
bool parse_octet(const std::string& s, std::size_t& i, int& v) {
    if (i >= s.size()) return false;
    std::size_t start = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    if (i == start) return false;             // no digits
    const std::size_t len = i - start;
    if (len > 3) return false;
    if (len > 1 && s[start] == '0') return false;  // reject leading zeros
    int val = 0;
    for (std::size_t k = start; k < i; ++k) {
        val = val * 10 + (s[k] - '0');
    }
    if (val > 255) return false;
    v = val;
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Address parsing
// ---------------------------------------------------------------------------

bool TargetWhitelist::parse_ipv4(const std::string& s, std::uint32_t& out) {
    if (s.empty()) return false;
    int parts[4] = {0, 0, 0, 0};
    std::size_t i = 0;
    for (int idx = 0; idx < 4; ++idx) {
        if (!parse_octet(s, i, parts[idx])) return false;
        if (idx < 3) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;  // consume '.'
        }
    }
    if (i != s.size()) return false;  // trailing junk
    out = (static_cast<std::uint32_t>(parts[0]) << 24) |
          (static_cast<std::uint32_t>(parts[1]) << 16) |
          (static_cast<std::uint32_t>(parts[2]) << 8) |
          static_cast<std::uint32_t>(parts[3]);
    return true;
}

bool TargetWhitelist::parse_ipv6(const std::string& s, std::uint8_t out[16]) {
    if (s.empty()) return false;

    // At most one "::" allowed.
    const std::size_t first_dc = s.find("::");
    const bool has_dc = (first_dc != std::string::npos);
    if (has_dc && s.find("::", first_dc + 2) != std::string::npos) return false;

    std::string left_str, right_str;
    if (has_dc) {
        left_str = s.substr(0, first_dc);
        right_str = s.substr(first_dc + 2);
    } else {
        right_str = s;
    }

    // Stray single ':' would create empty groups outside "::" -> reject.
    if (!left_str.empty() && (left_str.front() == ':' || left_str.back() == ':'))
        return false;
    if (!right_str.empty() && (right_str.front() == ':' || right_str.back() == ':'))
        return false;

    auto split_colon = [](const std::string& str) {
        std::vector<std::string> v;
        if (str.empty()) return v;
        std::size_t start = 0;
        for (;;) {
            auto pos = str.find(':', start);
            if (pos == std::string::npos) {
                v.push_back(str.substr(start));
                return v;
            }
            v.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }
    };

    auto left = split_colon(left_str);
    auto right = split_colon(right_str);

    // Optional IPv4 suffix: only the very last group may be an IPv4 literal,
    // which occupies the final two 16-bit groups.
    bool has_v4_suffix = false;
    std::uint32_t v4_val = 0;
    int v4_groups = 0;

    auto try_v4_suffix = [&](std::vector<std::string>& groups) -> bool {
        if (groups.empty()) return true;
        std::string& last = groups.back();
        if (last.find('.') == std::string::npos) return true;
        if (has_v4_suffix) return false;
        if (!parse_ipv4(last, v4_val)) return false;
        has_v4_suffix = true;
        v4_groups = 2;
        groups.pop_back();
        return true;
    };

    if (!right.empty()) {
        if (!try_v4_suffix(right)) return false;
    } else if (!left.empty()) {
        // Address like "left::" where IPv4 suffix sits at the end of left.
        if (!try_v4_suffix(left)) return false;
    }

    const int n_explicit = static_cast<int>(left.size() + right.size());
    const int total_groups = n_explicit + v4_groups;

    if (has_dc) {
        if (total_groups > 7) return false;  // "::" must expand to >= 1 group
    } else {
        if (total_groups != 8) return false;
    }

    auto parse_group = [](const std::string& g, std::uint16_t& v) -> bool {
        if (g.empty() || g.size() > 4) return false;
        unsigned int val = 0;
        for (char c : g) {
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else return false;
            val = val * 16 + static_cast<unsigned int>(d);
        }
        if (val > 0xFFFFu) return false;
        v = static_cast<std::uint16_t>(val);
        return true;
    };

    std::uint16_t groups[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int idx = 0;
    const int zero_count = 8 - total_groups;

    for (const auto& g : left) {
        std::uint16_t v = 0;
        if (!parse_group(g, v)) return false;
        groups[idx++] = v;
    }
    for (int z = 0; z < zero_count; ++z) groups[idx++] = 0;
    for (const auto& g : right) {
        std::uint16_t v = 0;
        if (!parse_group(g, v)) return false;
        groups[idx++] = v;
    }
    if (has_v4_suffix) {
        groups[idx++] = static_cast<std::uint16_t>((v4_val >> 16) & 0xFFFFu);
        groups[idx++] = static_cast<std::uint16_t>(v4_val & 0xFFFFu);
    }
    if (idx != 8) return false;  // sanity

    for (int i = 0; i < 8; ++i) {
        out[i * 2] = static_cast<std::uint8_t>((groups[i] >> 8) & 0xFFu);
        out[i * 2 + 1] = static_cast<std::uint8_t>(groups[i] & 0xFFu);
    }
    return true;
}

// ---------------------------------------------------------------------------
// CIDR parsing
// ---------------------------------------------------------------------------

void TargetWhitelist::make_v4_mask(std::uint32_t& m, int prefix) noexcept {
    if (prefix <= 0) {
        m = 0u;
    } else {
        // prefix in [1, 32] -> shift in [0, 31], well-defined.
        m = ~std::uint32_t{0} << (32 - prefix);
    }
}

void TargetWhitelist::make_v6_mask(std::uint8_t m[16], int prefix) noexcept {
    for (int i = 0; i < 16; ++i) m[i] = 0;
    for (int i = 0; i < prefix && i < 128; ++i) {
        m[i / 8] |= static_cast<std::uint8_t>(0x80 >> (i % 8));
    }
}

bool TargetWhitelist::parse_cidr_v4(const std::string& s, CidrV4& out) {
    const auto pos = s.find('/');
    if (pos == std::string::npos) return false;
    const std::string addr = s.substr(0, pos);
    const std::string pref = s.substr(pos + 1);
    if (pref.empty() || pref.size() > 2) return false;  // 0..32
    int prefix = 0;
    for (char c : pref) {
        if (c < '0' || c > '9') return false;
        prefix = prefix * 10 + (c - '0');
    }
    if (prefix > 32) return false;
    std::uint32_t ip = 0;
    if (!parse_ipv4(addr, ip)) return false;
    make_v4_mask(out.mask, prefix);
    out.net = ip & out.mask;
    return true;
}

bool TargetWhitelist::parse_cidr_v6(const std::string& s, CidrV6& out) {
    const auto pos = s.find('/');
    if (pos == std::string::npos) return false;
    const std::string addr = s.substr(0, pos);
    const std::string pref = s.substr(pos + 1);
    if (pref.empty() || pref.size() > 3) return false;  // 0..128
    int prefix = 0;
    for (char c : pref) {
        if (c < '0' || c > '9') return false;
        prefix = prefix * 10 + (c - '0');
    }
    if (prefix > 128) return false;
    std::uint8_t ip[16] = {0};
    if (!parse_ipv6(addr, ip)) return false;
    make_v6_mask(out.mask, prefix);
    for (int i = 0; i < 16; ++i) out.net[i] = ip[i] & out.mask[i];
    return true;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::variant<TargetWhitelist, std::string> TargetWhitelist::create(TargetPolicy policy) {
    TargetWhitelist w;
    w.policy_ = std::move(policy);

    // Validate ports: each must be 1..65535. Duplicates are dropped.
    for (int p : w.policy_.allowed_ports) {
        if (p < 1 || p > 65535) {
            return std::string("invalid port in allowed_ports: value out of range [1,65535]");
        }
        bool dup = false;
        for (std::uint16_t existing : w.ports_) {
            if (static_cast<int>(existing) == p) { dup = true; break; }
        }
        if (!dup) w.ports_.push_back(static_cast<std::uint16_t>(p));
    }

    // Validate self_listener_port (0 means unset).
    if (w.policy_.self_listener_port != 0) {
        if (w.policy_.self_listener_port < 1 ||
            w.policy_.self_listener_port > 65535) {
            return std::string("invalid self_listener_port: out of range [1,65535]");
        }
    }

    // Parse every CIDR. ':' present -> IPv6, '.' present -> IPv4, else malformed.
    for (const auto& c : w.policy_.allowed_cidrs) {
        if (c.empty()) {
            return std::string("empty CIDR string in allowed_cidrs");
        }
        const bool has_v6 = c.find(':') != std::string::npos;
        if (has_v6) {
            if (!w.policy_.allow_ipv6) {
                return "IPv6 CIDR present but allow_ipv6=false: " + c;
            }
            CidrV6 v;
            if (!parse_cidr_v6(c, v)) {
                return "invalid IPv6 CIDR: " + c;
            }
            w.cidrs_v6_.push_back(v);
        } else if (c.find('.') != std::string::npos) {
            CidrV4 v;
            if (!parse_cidr_v4(c, v)) {
                return "invalid IPv4 CIDR: " + c;
            }
            w.cidrs_v4_.push_back(v);
        } else {
            return "malformed CIDR (neither IPv4 nor IPv6): " + c;
        }
    }

    return w;
}

// ---------------------------------------------------------------------------
// check()
// ---------------------------------------------------------------------------

TargetCheckResult TargetWhitelist::check(const std::string& host,
                                         std::uint16_t port) const {
    TargetCheckResult r;

    // Rule 4: loopback protection. Checked first so it overrides everything.
    if (policy_.self_listener_port != 0 &&
        port == static_cast<std::uint16_t>(policy_.self_listener_port)) {
        r.reason = "target equals RemoteTool agent listener port (loopback protection)";
        return r;
    }

    // Rule 1: host must be an IP literal.
    std::uint32_t v4 = 0;
    std::uint8_t v6[16] = {0};
    const bool is_v4 = parse_ipv4(host, v4);
    const bool is_v6 = !is_v4 && parse_ipv6(host, v6);
    if (!is_v4 && !is_v6) {
        r.reason = "host is not an IP literal";
        return r;
    }

    // Rule 5: IPv6 targets require allow_ipv6=true.
    if (is_v6 && !policy_.allow_ipv6) {
        r.reason = "IPv6 target denied by policy (allow_ipv6=false)";
        return r;
    }

    // Rule 2: port must be listed (empty list = deny-all).
    if (ports_.empty()) {
        r.reason = "allowed_ports is empty (deny-all)";
        return r;
    }
    bool port_ok = false;
    for (std::uint16_t p : ports_) {
        if (p == port) { port_ok = true; break; }
    }
    if (!port_ok) {
        r.reason = "port not in allowed_ports";
        return r;
    }

    // Rule 3: host must fall in some allowed CIDR (empty list = deny-all).
    if (is_v4) {
        if (cidrs_v4_.empty()) {
            r.reason = "no IPv4 CIDR configured (deny-all)";
            return r;
        }
        for (const auto& c : cidrs_v4_) {
            if ((v4 & c.mask) == c.net) {
                r.allowed = true;
                return r;
            }
        }
        r.reason = "host does not match any allowed CIDR";
        return r;
    }

    // is_v6
    if (cidrs_v6_.empty()) {
        r.reason = "no IPv6 CIDR configured (deny-all)";
        return r;
    }
    for (const auto& c : cidrs_v6_) {
        bool match = true;
        for (int i = 0; i < 16; ++i) {
            if ((v6[i] & c.mask[i]) != c.net[i]) { match = false; break; }
        }
        if (match) {
            r.allowed = true;
            return r;
        }
    }
    r.reason = "host does not match any allowed CIDR";
    return r;
}

}  // namespace rmt::security
