// Schema validation for RemoteTool config files (CONFIG_SPEC.md sections
// 3/4/6/8, CODING_STANDARDS.md section 9, TEST_PLAN.md section 3.6).
//
// Implementation notes:
//   * Fail-fast: every validator returns std::optional<ConfigValidationError>;
//     the first non-nullopt return stops validation. This matches strict_json
//     and the task spec ("建议返回单个第一个错误").
//   * Strict type checks: is_int() rejects doubles/bools/strings; is_string()
//     rejects numbers; etc. strict_json already rejected comments/trailing
//     commas/duplicate keys, so we only do field-level semantic checks here.
//   * Unknown fields are rejected at every object level (CONFIG_SPEC.md
//     section 1, CODING_STANDARDS.md section 9).
//   * UTF-8 string lengths (display_name, mapping name) are counted in
//     Unicode codepoints; invalid UTF-8 byte sequences are rejected.
//   * IP literal / CIDR format validation is intentionally relaxed to
//     "non-empty string" per the task spec; runtime layers (socket bind,
//     target_whitelist) do the structural checks.
#include "rmt/config/config_schema.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rmt::config {
namespace {

// ---- shared helpers --------------------------------------------------------

ConfigValidationError make_err(std::string path, std::string reason) {
    return {std::move(path), std::move(reason)};
}

// Build a path like "devices[3].id" or "mappings[0]".
std::string elem_path(const std::string& array_path, std::size_t index,
                      std::string_view field) {
    std::string out = array_path;
    out.push_back('[');
    out += std::to_string(index);
    out.push_back(']');
    if (!field.empty()) {
        out.push_back('.');
        out.append(field);
    }
    return out;
}

// Count Unicode codepoints in a UTF-8 string. Returns nullopt on invalid
// UTF-8 (overlong/illegal sequences). strict_json does not byte-validate
// UTF-8 in string contents (only rejects BOM), so this is where we catch
// malformed UTF-8 in display_name / mapping name fields.
std::optional<std::size_t> count_utf8_codepoints(std::string_view s) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len;
        if (c < 0x80) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (c < 0xC2) return std::nullopt;  // overlong 2-byte
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (c > 0xF4) return std::nullopt;  // > U+10FFFF
            len = 4;
        } else {
            return std::nullopt;  // invalid leading byte
        }
        if (i + len > s.size()) return std::nullopt;
        for (std::size_t j = 1; j < len; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
                return std::nullopt;
            }
        }
        // Reject overlong 3-byte and 4-byte sequences.
        if (len == 3) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xE0 && (b1 & 0xE0) == 0x80) return std::nullopt;
        }
        if (len == 4) {
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xF0 && (b1 & 0xF0) == 0x80) return std::nullopt;
            if (c == 0xF4 && b1 > 0x8F) return std::nullopt;  // > U+10FFFF
        }
        ++count;
        i += len;
    }
    return count;
}

// device_id charset: ASCII letters, digits, '-', '_', '.' (CONFIG_SPEC.md
// section 4).
bool is_valid_device_id_charset(std::string_view s) {
    for (char c : s) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
    }
    return true;
}

// pairing_code: empty or exactly 8 ASCII digits (CONFIG_SPEC.md section 8).
bool is_valid_pairing_code(std::string_view s) {
    if (s.empty()) return true;
    if (s.size() != 8) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

bool is_allowed_key(std::string_view key,
                    const std::vector<std::string_view>& allowed) {
    for (auto k : allowed) {
        if (k == key) return true;
    }
    return false;
}

// Check that `obj` contains only keys in `allowed`. Returns the first
// unknown key as an error if any.
std::optional<ConfigValidationError> check_unknown_keys(
    const JsonObject& obj, const std::vector<std::string_view>& allowed,
    const std::string& path) {
    for (const auto& kv : obj) {
        if (!is_allowed_key(kv.first, allowed)) {
            return make_err(path.empty() ? kv.first : path + "." + kv.first,
                            "unknown field");
        }
    }
    return std::nullopt;
}

// Validate a logging sub-object (shared by remote_tool.json and agent.json).
// `path` is the dotted path to the logging object (e.g. "logging").
std::optional<ConfigValidationError> validate_logging_block(
    const JsonValue& logging, const std::string& path) {
    if (!logging.is_object()) {
        return make_err(path, "must be an object");
    }
    const JsonObject* obj = logging.try_as_object();
    static const std::vector<std::string_view> kAllowed = {
        "level", "max_file_bytes", "retained_files"};
    if (auto e = check_unknown_keys(*obj, kAllowed, path)) return e;

    const JsonValue* level = logging.find("level");
    if (level == nullptr) {
        return make_err(path + ".level", "missing required field");
    }
    if (!level->is_string()) {
        return make_err(path + ".level", "must be a string");
    }
    const std::string* lvl = level->try_as_string();
    if (*lvl != "debug" && *lvl != "info" && *lvl != "warn" && *lvl != "error") {
        return make_err(path + ".level",
                        "must be one of debug/info/warn/error");
    }

    const JsonValue* mfb = logging.find("max_file_bytes");
    if (mfb == nullptr) {
        return make_err(path + ".max_file_bytes", "missing required field");
    }
    if (!mfb->is_int()) {
        return make_err(path + ".max_file_bytes", "must be an integer");
    }
    if (mfb->as_int().value() <= 0) {
        return make_err(path + ".max_file_bytes", "must be > 0");
    }

    const JsonValue* rf = logging.find("retained_files");
    if (rf == nullptr) {
        return make_err(path + ".retained_files", "missing required field");
    }
    if (!rf->is_int()) {
        return make_err(path + ".retained_files", "must be an integer");
    }
    if (rf->as_int().value() <= 0) {
        return make_err(path + ".retained_files", "must be > 0");
    }

    return std::nullopt;
}

// ---- remote_tool.json (CONFIG_SPEC.md section 3) --------------------------

std::optional<ConfigValidationError> validate_remote_tool(const JsonValue& root) {
    if (!root.is_object()) {
        return make_err("", "root must be an object");
    }
    const JsonObject* obj = root.try_as_object();

    static const std::vector<std::string_view> kRootAllowed = {
        "schema_version", "agent_listener", "mapping_listener", "heartbeat",
        "limits", "logging"};
    if (auto e = check_unknown_keys(*obj, kRootAllowed, "")) return e;

    // schema_version
    const JsonValue* sv = root.find("schema_version");
    if (sv == nullptr) {
        return make_err("schema_version", "missing required field");
    }
    if (!sv->is_int()) {
        return make_err("schema_version", "must be an integer");
    }
    if (sv->as_int().value() != 1) {
        return make_err("schema_version", "must be 1");
    }

    // agent_listener
    const JsonValue* al = root.find("agent_listener");
    if (al == nullptr) {
        return make_err("agent_listener", "missing required field");
    }
    if (!al->is_object()) {
        return make_err("agent_listener", "must be an object");
    }
    {
        static const std::vector<std::string_view> kAllowed = {"bind_host", "port"};
        if (auto e = check_unknown_keys(*al->try_as_object(), kAllowed,
                                        "agent_listener")) {
            return e;
        }
        const JsonValue* bh = al->find("bind_host");
        if (bh == nullptr) {
            return make_err("agent_listener.bind_host", "missing required field");
        }
        if (!bh->is_string()) {
            return make_err("agent_listener.bind_host", "must be a string");
        }
        if (bh->try_as_string()->empty()) {
            return make_err("agent_listener.bind_host", "must be non-empty");
        }
        const JsonValue* port = al->find("port");
        if (port == nullptr) {
            return make_err("agent_listener.port", "missing required field");
        }
        if (!port->is_int()) {
            return make_err("agent_listener.port", "must be an integer");
        }
        long long pv = port->as_int().value();
        if (pv < 1 || pv > 65535) {
            return make_err("agent_listener.port", "must be in range 1-65535");
        }
    }

    // mapping_listener: MVP must be exactly "127.0.0.1".
    const JsonValue* ml = root.find("mapping_listener");
    if (ml == nullptr) {
        return make_err("mapping_listener", "missing required field");
    }
    if (!ml->is_object()) {
        return make_err("mapping_listener", "must be an object");
    }
    {
        static const std::vector<std::string_view> kAllowed = {"bind_host"};
        if (auto e = check_unknown_keys(*ml->try_as_object(), kAllowed,
                                        "mapping_listener")) {
            return e;
        }
        const JsonValue* bh = ml->find("bind_host");
        if (bh == nullptr) {
            return make_err("mapping_listener.bind_host", "missing required field");
        }
        if (!bh->is_string()) {
            return make_err("mapping_listener.bind_host", "must be a string");
        }
        if (*bh->try_as_string() != "127.0.0.1") {
            return make_err("mapping_listener.bind_host",
                            "MVP requires 127.0.0.1");
        }
    }

    // heartbeat
    const JsonValue* hb = root.find("heartbeat");
    if (hb == nullptr) {
        return make_err("heartbeat", "missing required field");
    }
    if (!hb->is_object()) {
        return make_err("heartbeat", "must be an object");
    }
    long long interval_ms = 0;
    {
        static const std::vector<std::string_view> kAllowed = {
            "interval_ms", "timeout_ms"};
        if (auto e = check_unknown_keys(*hb->try_as_object(), kAllowed,
                                        "heartbeat")) {
            return e;
        }
        const JsonValue* iv = hb->find("interval_ms");
        if (iv == nullptr) {
            return make_err("heartbeat.interval_ms", "missing required field");
        }
        if (!iv->is_int()) {
            return make_err("heartbeat.interval_ms", "must be an integer");
        }
        interval_ms = iv->as_int().value();
        if (interval_ms < 1000 || interval_ms > 60000) {
            return make_err("heartbeat.interval_ms",
                            "must be in range 1000-60000");
        }
        const JsonValue* tv = hb->find("timeout_ms");
        if (tv == nullptr) {
            return make_err("heartbeat.timeout_ms", "missing required field");
        }
        if (!tv->is_int()) {
            return make_err("heartbeat.timeout_ms", "must be an integer");
        }
        long long timeout_ms = tv->as_int().value();
        if (timeout_ms < 2 * interval_ms) {
            return make_err("heartbeat.timeout_ms",
                            "must be at least 2 * interval_ms");
        }
        if (timeout_ms > 180000) {
            return make_err("heartbeat.timeout_ms",
                            "must be <= 180000");
        }
    }

    // limits
    const JsonValue* lm = root.find("limits");
    if (lm == nullptr) {
        return make_err("limits", "missing required field");
    }
    if (!lm->is_object()) {
        return make_err("limits", "must be an object");
    }
    {
        static const std::vector<std::string_view> kAllowed = {
            "max_sessions_per_mapping", "max_sessions_per_agent",
            "max_session_queue_bytes", "max_tunnel_queue_bytes"};
        if (auto e = check_unknown_keys(*lm->try_as_object(), kAllowed, "limits")) {
            return e;
        }
        const JsonValue* mspm = lm->find("max_sessions_per_mapping");
        if (mspm == nullptr) {
            return make_err("limits.max_sessions_per_mapping",
                            "missing required field");
        }
        if (!mspm->is_int()) {
            return make_err("limits.max_sessions_per_mapping",
                            "must be an integer");
        }
        long long per_mapping = mspm->as_int().value();
        if (per_mapping < 1 || per_mapping > 1024) {
            return make_err("limits.max_sessions_per_mapping",
                            "must be in range 1-1024");
        }

        const JsonValue* mspa = lm->find("max_sessions_per_agent");
        if (mspa == nullptr) {
            return make_err("limits.max_sessions_per_agent",
                            "missing required field");
        }
        if (!mspa->is_int()) {
            return make_err("limits.max_sessions_per_agent",
                            "must be an integer");
        }
        long long per_agent = mspa->as_int().value();
        if (per_agent < per_mapping) {
            return make_err("limits.max_sessions_per_agent",
                            "must be >= max_sessions_per_mapping");
        }
        if (per_agent > 4096) {
            return make_err("limits.max_sessions_per_agent",
                            "must be <= 4096");
        }

        const JsonValue* msqb = lm->find("max_session_queue_bytes");
        if (msqb == nullptr) {
            return make_err("limits.max_session_queue_bytes",
                            "missing required field");
        }
        if (!msqb->is_int()) {
            return make_err("limits.max_session_queue_bytes",
                            "must be an integer");
        }
        long long session_q = msqb->as_int().value();
        if (session_q < 65536 || session_q > 4194304) {
            return make_err("limits.max_session_queue_bytes",
                            "must be in range 65536-4194304");
        }

        const JsonValue* mtqb = lm->find("max_tunnel_queue_bytes");
        if (mtqb == nullptr) {
            return make_err("limits.max_tunnel_queue_bytes",
                            "missing required field");
        }
        if (!mtqb->is_int()) {
            return make_err("limits.max_tunnel_queue_bytes",
                            "must be an integer");
        }
        long long tunnel_q = mtqb->as_int().value();
        if (tunnel_q < session_q) {
            return make_err("limits.max_tunnel_queue_bytes",
                            "must be >= max_session_queue_bytes");
        }
        if (tunnel_q > 67108864) {
            return make_err("limits.max_tunnel_queue_bytes",
                            "must be <= 67108864");
        }
    }

    // logging
    const JsonValue* lg = root.find("logging");
    if (lg == nullptr) {
        return make_err("logging", "missing required field");
    }
    if (auto e = validate_logging_block(*lg, "logging")) return e;

    return std::nullopt;
}

// ---- devices.json (CONFIG_SPEC.md section 4) ------------------------------

std::optional<ConfigValidationError> validate_devices(const JsonValue& root) {
    if (!root.is_object()) {
        return make_err("", "root must be an object");
    }
    const JsonObject* obj = root.try_as_object();

    static const std::vector<std::string_view> kRootAllowed = {
        "schema_version", "devices"};
    if (auto e = check_unknown_keys(*obj, kRootAllowed, "")) return e;

    const JsonValue* sv = root.find("schema_version");
    if (sv == nullptr) {
        return make_err("schema_version", "missing required field");
    }
    if (!sv->is_int()) {
        return make_err("schema_version", "must be an integer");
    }
    if (sv->as_int().value() != 1) {
        return make_err("schema_version", "must be 1");
    }

    const JsonValue* devices_v = root.find("devices");
    if (devices_v == nullptr) {
        return make_err("devices", "missing required field");
    }
    if (!devices_v->is_array()) {
        return make_err("devices", "must be an array");
    }
    const JsonArray* arr = devices_v->try_as_array();

    static const std::vector<std::string_view> kDeviceAllowed = {
        "id", "display_name", "enabled", "device_key_dpapi",
        "created_unix_ms", "updated_unix_ms"};

    std::unordered_set<std::string> seen_ids;
    for (std::size_t i = 0; i < arr->size(); ++i) {
        const JsonValue& elem = (*arr)[i];
        std::string base = "devices";
        std::string elem_path_id = elem_path(base, i, "id");
        std::string elem_path_base = elem_path(base, i, "");

        if (!elem.is_object()) {
            return make_err(elem_path_base, "device element must be an object");
        }
        const JsonObject* eobj = elem.try_as_object();
        if (auto e = check_unknown_keys(*eobj, kDeviceAllowed, elem_path_base)) {
            return e;
        }

        const JsonValue* id = elem.find("id");
        if (id == nullptr) {
            return make_err(elem_path_id, "missing required field");
        }
        if (!id->is_string()) {
            return make_err(elem_path_id, "must be a string");
        }
        const std::string& id_str = *id->try_as_string();
        if (id_str.empty() || id_str.size() > 64) {
            return make_err(elem_path_id, "must be 1-64 characters");
        }
        if (!is_valid_device_id_charset(id_str)) {
            return make_err(elem_path_id,
                            "contains illegal character (allowed: A-Z a-z 0-9 - _ .)");
        }
        if (seen_ids.count(id_str) > 0) {
            return make_err(elem_path_id, "duplicate device id");
        }
        seen_ids.insert(id_str);

        const JsonValue* dn = elem.find("display_name");
        std::string dn_path = elem_path(base, i, "display_name");
        if (dn == nullptr) {
            return make_err(dn_path, "missing required field");
        }
        if (!dn->is_string()) {
            return make_err(dn_path, "must be a string");
        }
        const std::string& dn_str = *dn->try_as_string();
        auto dn_count = count_utf8_codepoints(dn_str);
        if (!dn_count) {
            return make_err(dn_path, "invalid UTF-8 encoding");
        }
        if (*dn_count < 1 || *dn_count > 128) {
            return make_err(dn_path,
                            "must be 1-128 Unicode characters");
        }

        const JsonValue* en = elem.find("enabled");
        std::string en_path = elem_path(base, i, "enabled");
        if (en == nullptr) {
            return make_err(en_path, "missing required field");
        }
        if (!en->is_bool()) {
            return make_err(en_path, "must be a boolean");
        }

        const JsonValue* key = elem.find("device_key_dpapi");
        std::string key_path = elem_path(base, i, "device_key_dpapi");
        if (key == nullptr) {
            return make_err(key_path, "missing required field");
        }
        if (!key->is_string()) {
            return make_err(key_path, "must be a string");
        }
        // Empty string is allowed (pending pairing state).

        const JsonValue* created = elem.find("created_unix_ms");
        std::string created_path = elem_path(base, i, "created_unix_ms");
        if (created == nullptr) {
            return make_err(created_path, "missing required field");
        }
        if (!created->is_int()) {
            return make_err(created_path, "must be an integer");
        }
        if (created->as_int().value() < 0) {
            return make_err(created_path, "must be >= 0");
        }

        const JsonValue* updated = elem.find("updated_unix_ms");
        std::string updated_path = elem_path(base, i, "updated_unix_ms");
        if (updated == nullptr) {
            return make_err(updated_path, "missing required field");
        }
        if (!updated->is_int()) {
            return make_err(updated_path, "must be an integer");
        }
        if (updated->as_int().value() < 0) {
            return make_err(updated_path, "must be >= 0");
        }
    }

    return std::nullopt;
}

// ---- mappings.json (CONFIG_SPEC.md section 6) -----------------------------

std::optional<ConfigValidationError> validate_mappings(const JsonValue& root) {
    if (!root.is_object()) {
        return make_err("", "root must be an object");
    }
    const JsonObject* obj = root.try_as_object();

    static const std::vector<std::string_view> kRootAllowed = {
        "schema_version", "mappings"};
    if (auto e = check_unknown_keys(*obj, kRootAllowed, "")) return e;

    const JsonValue* sv = root.find("schema_version");
    if (sv == nullptr) {
        return make_err("schema_version", "missing required field");
    }
    if (!sv->is_int()) {
        return make_err("schema_version", "must be an integer");
    }
    if (sv->as_int().value() != 1) {
        return make_err("schema_version", "must be 1");
    }

    const JsonValue* mappings_v = root.find("mappings");
    if (mappings_v == nullptr) {
        return make_err("mappings", "missing required field");
    }
    if (!mappings_v->is_array()) {
        return make_err("mappings", "must be an array");
    }
    const JsonArray* arr = mappings_v->try_as_array();

    static const std::vector<std::string_view> kMappingAllowed = {
        "id", "device_id", "name", "local_port", "target_host",
        "target_port", "enabled", "connect_timeout_ms"};

    std::unordered_set<std::string> seen_ids;
    std::unordered_set<long long> seen_local_ports;
    for (std::size_t i = 0; i < arr->size(); ++i) {
        const JsonValue& elem = (*arr)[i];
        std::string base = "mappings";
        std::string elem_path_base = elem_path(base, i, "");

        if (!elem.is_object()) {
            return make_err(elem_path_base, "mapping element must be an object");
        }
        const JsonObject* eobj = elem.try_as_object();
        if (auto e = check_unknown_keys(*eobj, kMappingAllowed, elem_path_base)) {
            return e;
        }

        const JsonValue* id = elem.find("id");
        std::string id_path = elem_path(base, i, "id");
        if (id == nullptr) {
            return make_err(id_path, "missing required field");
        }
        if (!id->is_string()) {
            return make_err(id_path, "must be a string");
        }
        const std::string& id_str = *id->try_as_string();
        auto id_count = count_utf8_codepoints(id_str);
        if (!id_count) {
            return make_err(id_path, "invalid UTF-8 encoding");
        }
        if (*id_count < 1 || *id_count > 64) {
            return make_err(id_path, "must be 1-64 characters");
        }
        if (seen_ids.count(id_str) > 0) {
            return make_err(id_path, "duplicate mapping id");
        }
        seen_ids.insert(id_str);

        const JsonValue* did = elem.find("device_id");
        std::string did_path = elem_path(base, i, "device_id");
        if (did == nullptr) {
            return make_err(did_path, "missing required field");
        }
        if (!did->is_string()) {
            return make_err(did_path, "must be a string");
        }
        if (did->try_as_string()->empty()) {
            return make_err(did_path, "must be non-empty");
        }
        // Cross-file reference check (device_id exists in devices.json) is
        // intentionally NOT done here; it requires multiple loaded files.

        const JsonValue* name = elem.find("name");
        std::string name_path = elem_path(base, i, "name");
        if (name == nullptr) {
            return make_err(name_path, "missing required field");
        }
        if (!name->is_string()) {
            return make_err(name_path, "must be a string");
        }
        const std::string& name_str = *name->try_as_string();
        auto name_count = count_utf8_codepoints(name_str);
        if (!name_count) {
            return make_err(name_path, "invalid UTF-8 encoding");
        }
        if (*name_count < 1 || *name_count > 64) {
            return make_err(name_path, "must be 1-64 Unicode characters");
        }

        const JsonValue* lp = elem.find("local_port");
        std::string lp_path = elem_path(base, i, "local_port");
        if (lp == nullptr) {
            return make_err(lp_path, "missing required field");
        }
        if (!lp->is_int()) {
            return make_err(lp_path, "must be an integer");
        }
        long long lpv = lp->as_int().value();
        if (lpv < 1 || lpv > 65535) {
            return make_err(lp_path, "must be in range 1-65535");
        }
        // MVP: uniqueness across all mappings (enabled or not).
        if (seen_local_ports.count(lpv) > 0) {
            return make_err(lp_path, "duplicate local_port across mappings");
        }
        seen_local_ports.insert(lpv);

        const JsonValue* th = elem.find("target_host");
        std::string th_path = elem_path(base, i, "target_host");
        if (th == nullptr) {
            return make_err(th_path, "missing required field");
        }
        if (!th->is_string()) {
            return make_err(th_path, "must be a string");
        }
        if (th->try_as_string()->empty()) {
            return make_err(th_path, "must be non-empty");
        }
        // IP literal format check is deferred to the runtime target_whitelist.

        const JsonValue* tp = elem.find("target_port");
        std::string tp_path = elem_path(base, i, "target_port");
        if (tp == nullptr) {
            return make_err(tp_path, "missing required field");
        }
        if (!tp->is_int()) {
            return make_err(tp_path, "must be an integer");
        }
        long long tpv = tp->as_int().value();
        if (tpv < 1 || tpv > 65535) {
            return make_err(tp_path, "must be in range 1-65535");
        }

        const JsonValue* en = elem.find("enabled");
        std::string en_path = elem_path(base, i, "enabled");
        if (en == nullptr) {
            return make_err(en_path, "missing required field");
        }
        if (!en->is_bool()) {
            return make_err(en_path, "must be a boolean");
        }

        const JsonValue* ct = elem.find("connect_timeout_ms");
        std::string ct_path = elem_path(base, i, "connect_timeout_ms");
        if (ct == nullptr) {
            return make_err(ct_path, "missing required field");
        }
        if (!ct->is_int()) {
            return make_err(ct_path, "must be an integer");
        }
        long long ctv = ct->as_int().value();
        if (ctv < 1000 || ctv > 30000) {
            return make_err(ct_path, "must be in range 1000-30000");
        }
    }

    return std::nullopt;
}

// ---- agent.json (CONFIG_SPEC.md section 8) --------------------------------

std::optional<ConfigValidationError> validate_agent(const JsonValue& root) {
    if (!root.is_object()) {
        return make_err("", "root must be an object");
    }
    const JsonObject* obj = root.try_as_object();

    static const std::vector<std::string_view> kRootAllowed = {
        "schema_version", "server", "device", "target_policy", "logging"};
    if (auto e = check_unknown_keys(*obj, kRootAllowed, "")) return e;

    const JsonValue* sv = root.find("schema_version");
    if (sv == nullptr) {
        return make_err("schema_version", "missing required field");
    }
    if (!sv->is_int()) {
        return make_err("schema_version", "must be an integer");
    }
    if (sv->as_int().value() != 1) {
        return make_err("schema_version", "must be 1");
    }

    // server
    const JsonValue* server = root.find("server");
    if (server == nullptr) {
        return make_err("server", "missing required field");
    }
    if (!server->is_object()) {
        return make_err("server", "must be an object");
    }
    {
        static const std::vector<std::string_view> kAllowed = {"host", "port"};
        if (auto e = check_unknown_keys(*server->try_as_object(), kAllowed,
                                        "server")) {
            return e;
        }
        const JsonValue* host = server->find("host");
        if (host == nullptr) {
            return make_err("server.host", "missing required field");
        }
        if (!host->is_string()) {
            return make_err("server.host", "must be a string");
        }
        if (host->try_as_string()->empty()) {
            return make_err("server.host", "must be non-empty");
        }
        const JsonValue* port = server->find("port");
        if (port == nullptr) {
            return make_err("server.port", "missing required field");
        }
        if (!port->is_int()) {
            return make_err("server.port", "must be an integer");
        }
        long long pv = port->as_int().value();
        if (pv < 1 || pv > 65535) {
            return make_err("server.port", "must be in range 1-65535");
        }
    }

    // device
    const JsonValue* device = root.find("device");
    if (device == nullptr) {
        return make_err("device", "missing required field");
    }
    if (!device->is_object()) {
        return make_err("device", "must be an object");
    }
    {
        static const std::vector<std::string_view> kAllowed = {
            "id", "pairing_code", "device_key_dpapi"};
        if (auto e = check_unknown_keys(*device->try_as_object(), kAllowed,
                                        "device")) {
            return e;
        }
        const JsonValue* id = device->find("id");
        if (id == nullptr) {
            return make_err("device.id", "missing required field");
        }
        if (!id->is_string()) {
            return make_err("device.id", "must be a string");
        }
        const std::string& id_str = *id->try_as_string();
        if (id_str.empty() || id_str.size() > 64) {
            return make_err("device.id", "must be 1-64 characters");
        }
        if (!is_valid_device_id_charset(id_str)) {
            return make_err("device.id",
                            "contains illegal character (allowed: A-Z a-z 0-9 - _ .)");
        }

        const JsonValue* pc = device->find("pairing_code");
        if (pc == nullptr) {
            return make_err("device.pairing_code", "missing required field");
        }
        if (!pc->is_string()) {
            return make_err("device.pairing_code", "must be a string");
        }
        if (!is_valid_pairing_code(*pc->try_as_string())) {
            return make_err("device.pairing_code",
                            "must be empty or exactly 8 digits");
        }

        const JsonValue* key = device->find("device_key_dpapi");
        if (key == nullptr) {
            return make_err("device.device_key_dpapi", "missing required field");
        }
        if (!key->is_string()) {
            return make_err("device.device_key_dpapi", "must be a string");
        }
        // Empty string is allowed (pre-pairing state).
    }

    // target_policy
    const JsonValue* tp = root.find("target_policy");
    if (tp == nullptr) {
        return make_err("target_policy", "missing required field");
    }
    if (!tp->is_object()) {
        return make_err("target_policy", "must be an object");
    }
    {
        static const std::vector<std::string_view> kAllowed = {
            "allowed_cidrs", "allowed_ports", "allow_ipv6"};
        if (auto e = check_unknown_keys(*tp->try_as_object(), kAllowed,
                                        "target_policy")) {
            return e;
        }
        const JsonValue* cidrs = tp->find("allowed_cidrs");
        if (cidrs == nullptr) {
            return make_err("target_policy.allowed_cidrs",
                            "missing required field");
        }
        if (!cidrs->is_array()) {
            return make_err("target_policy.allowed_cidrs", "must be an array");
        }
        const JsonArray* cidrs_arr = cidrs->try_as_array();
        for (std::size_t i = 0; i < cidrs_arr->size(); ++i) {
            const JsonValue& c = (*cidrs_arr)[i];
            std::string p = "target_policy.allowed_cidrs[" + std::to_string(i) + "]";
            if (!c.is_string()) {
                return make_err(p, "must be a string");
            }
            if (c.try_as_string()->empty()) {
                return make_err(p, "must be non-empty");
            }
            // CIDR format validation is deferred to the runtime target_whitelist.
        }

        const JsonValue* ports = tp->find("allowed_ports");
        if (ports == nullptr) {
            return make_err("target_policy.allowed_ports",
                            "missing required field");
        }
        if (!ports->is_array()) {
            return make_err("target_policy.allowed_ports", "must be an array");
        }
        const JsonArray* ports_arr = ports->try_as_array();
        std::unordered_set<long long> seen_ports;
        for (std::size_t i = 0; i < ports_arr->size(); ++i) {
            const JsonValue& p_v = (*ports_arr)[i];
            std::string p = "target_policy.allowed_ports[" + std::to_string(i) + "]";
            if (!p_v.is_int()) {
                return make_err(p, "must be an integer");
            }
            long long pv = p_v.as_int().value();
            if (pv < 1 || pv > 65535) {
                return make_err(p, "must be in range 1-65535");
            }
            if (seen_ports.count(pv) > 0) {
                return make_err(p, "duplicate port in allowed_ports");
            }
            seen_ports.insert(pv);
        }

        const JsonValue* v6 = tp->find("allow_ipv6");
        if (v6 == nullptr) {
            return make_err("target_policy.allow_ipv6", "missing required field");
        }
        if (!v6->is_bool()) {
            return make_err("target_policy.allow_ipv6", "must be a boolean");
        }
    }

    // logging
    const JsonValue* lg = root.find("logging");
    if (lg == nullptr) {
        return make_err("logging", "missing required field");
    }
    if (auto e = validate_logging_block(*lg, "logging")) return e;

    return std::nullopt;
}

}  // namespace

// ---- public API ------------------------------------------------------------

ConfigValidationResult validate_config(ConfigFileKind kind, const JsonValue& root) {
    std::optional<ConfigValidationError> err;
    switch (kind) {
        case ConfigFileKind::RemoteTool:
            err = validate_remote_tool(root);
            break;
        case ConfigFileKind::Devices:
            err = validate_devices(root);
            break;
        case ConfigFileKind::Mappings:
            err = validate_mappings(root);
            break;
        case ConfigFileKind::Agent:
            err = validate_agent(root);
            break;
    }
    if (err) {
        return std::move(*err);
    }
    return JsonValue(root);
}

ConfigValidationResult validate_config_text(ConfigFileKind kind,
                                            std::string_view text) {
    JsonParseResult parse_result = parse_json(text);
    if (const JsonParseError* perr = try_get_json_error(parse_result)) {
        ConfigValidationError e;
        e.field_path = "<root>";
        e.reason = "JSON parse error at line " + std::to_string(perr->line) +
                   ", column " + std::to_string(perr->column) + ": " + perr->reason;
        return e;
    }
    return validate_config(kind, *try_get_json_value(parse_result));
}

}  // namespace rmt::config
