#include "rmt/config/config_loader.h"
#include "rmt/config/atomic_write.h"
#include "rmt/config/config_schema.h"
#include "rmt/config/strict_json.h"

#include <fstream>
#include <sstream>
#include <string>

namespace rmt::config {
namespace {

// Read entire file into a string. Returns empty string on error (caller
// checks and emits ConfigLoadError).
std::string read_file(const std::string& path, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open file: " + path;
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (!f) {
        err = "read error: " + path;
        return {};
    }
    return ss.str();
}

// Parse + validate a config file. Returns the validated JsonValue on
// success, or populates `out_err` on failure.
bool parse_and_validate(const std::string& path, ConfigFileKind kind,
                        const std::string& text, JsonValue& out_val,
                        ConfigLoadError& out_err) {
    auto result = validate_config_text(kind, text);
    if (auto* val = try_get_valid_config(result)) {
        out_val = *val;
        return true;
    }
    auto* err = try_get_config_error(result);
    out_err.path = path;
    out_err.reason = err ? (err->field_path + ": " + err->reason) : "validation failed";
    return false;
}

// ---- JSON ⟷ struct helpers ----

// Generic: read an integer field.
bool read_int(const JsonValue& obj, const char* key, int& out) {
    auto* v = obj.find(key);
    if (!v) return false;
    auto opt = v->as_int();
    if (!opt) return false;
    out = static_cast<int>(*opt);
    return true;
}

bool read_long_long(const JsonValue& obj, const char* key, long long& out) {
    auto* v = obj.find(key);
    if (!v) return false;
    auto opt = v->as_int();
    if (!opt) return false;
    out = *opt;
    return true;
}

bool read_string(const JsonValue& obj, const char* key, std::string& out) {
    auto* v = obj.find(key);
    if (!v) return false;
    auto* s = v->try_as_string();
    if (!s) return false;
    out = *s;
    return true;
}

bool read_bool(const JsonValue& obj, const char* key, bool& out) {
    auto* v = obj.find(key);
    if (!v) return false;
    auto opt = v->as_bool();
    if (!opt) return false;
    out = *opt;
    return true;
}

bool read_string_array(const JsonValue& obj, const char* key,
                       std::vector<std::string>& out) {
    auto* v = obj.find(key);
    if (!v) return false;
    auto* arr = v->try_as_array();
    if (!arr) return false;
    out.clear();
    for (std::size_t i = 0; i < arr->size(); ++i) {
        auto& elem = (*arr)[i];
        auto* s = elem.try_as_string();
        if (!s) return false;
        out.push_back(*s);
    }
    return true;
}

bool read_int_array(const JsonValue& obj, const char* key,
                    std::vector<int>& out) {
    auto* v = obj.find(key);
    if (!v) return false;
    auto* arr = v->try_as_array();
    if (!arr) return false;
    out.clear();
    for (std::size_t i = 0; i < arr->size(); ++i) {
        auto& elem = (*arr)[i];
        auto opt = elem.as_int();
        if (!opt) return false;
        out.push_back(static_cast<int>(*opt));
    }
    return true;
}

// ---- JSON builder (for save) ----

std::string make_json_string(const std::string& s) {
    // Minimal JSON string escaping (enough for config values which are
    // ASCII alphanum / dotted paths / standard characters).
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c;
        }
    }
    out += "\"";
    return out;
}

std::string json_obj(int indent, const std::string& content) {
    std::string in(indent, ' ');
    std::string in1(indent + 2, ' ');
    return "{\n" + in1 + content + "\n" + in + "}";
}

std::string json_arr(int indent, const std::string& content) {
    if (content.empty()) return "[]";
    std::string in(indent + 2, ' ');
    std::string out = "[\n";
    out += content;
    out += "\n" + std::string(indent, ' ') + "]";
    return out;
}

std::string kv(const char* k, const std::string& v) {
    return std::string("\"") + k + "\": " + v;
}

std::string kv_str(const char* k, const std::string& v) {
    return kv(k, make_json_string(v));
}

std::string kv_int(const char* k, int v) {
    return kv(k, std::to_string(v));
}

std::string kv_long_long(const char* k, long long v) {
    return kv(k, std::to_string(v));
}

std::string kv_bool(const char* k, bool v) {
    return kv(k, v ? "true" : "false");
}

}  // namespace

// ===== RemoteTool =====

LoadResult<RemoteToolConfig> load_remote_tool_config(const std::string& path) {
    RemoteToolConfig cfg;
    std::string err_str;
    std::string text = read_file(path, err_str);
    if (text.empty() && !err_str.empty()) {
        return ConfigLoadError{path, err_str};
    }

    JsonValue root;
    ConfigLoadError load_err;
    if (!parse_and_validate(path, ConfigFileKind::RemoteTool, text, root, load_err)) {
        return load_err;
    }

    // Read top-level fields (schema_version already validated by config_schema).
    read_int(root, "schema_version", cfg.schema_version);

    auto* al = root.find("agent_listener");
    if (al) { read_string(*al, "bind_host", cfg.bind_host); read_int(*al, "port", cfg.agent_port); }

    auto* ml = root.find("mapping_listener");
    if (ml) read_string(*ml, "bind_host", cfg.mapping_bind_host);

    auto* hb = root.find("heartbeat");
    if (hb) { read_int(*hb, "interval_ms", cfg.heartbeat_interval_ms); read_int(*hb, "timeout_ms", cfg.heartbeat_timeout_ms); }

    auto* lm = root.find("limits");
    if (lm) {
        read_int(*lm, "max_sessions_per_mapping", cfg.max_sessions_per_mapping);
        read_int(*lm, "max_sessions_per_agent", cfg.max_sessions_per_agent);
        read_int(*lm, "max_session_queue_bytes", cfg.max_session_queue_bytes);
        read_int(*lm, "max_tunnel_queue_bytes", cfg.max_tunnel_queue_bytes);
    }

    auto* lg = root.find("logging");
    if (lg) {
        read_string(*lg, "level", cfg.log_level);
        read_int(*lg, "max_file_bytes", cfg.log_max_file_bytes);
        read_int(*lg, "retained_files", cfg.log_retained_files);
    }

    return cfg;
}

SaveResult save_remote_tool_config(const std::string& path,
                                    const RemoteToolConfig& cfg) {
    auto al = json_obj(2,
        kv_str("bind_host", cfg.bind_host) + ",\n" +
        kv_int("port", cfg.agent_port));
    auto ml = json_obj(2,
        kv_str("bind_host", cfg.mapping_bind_host));
    auto hb = json_obj(2,
        kv_int("interval_ms", cfg.heartbeat_interval_ms) + ",\n" +
        kv_int("timeout_ms", cfg.heartbeat_timeout_ms));
    auto limits = json_obj(2,
        kv_int("max_sessions_per_mapping", cfg.max_sessions_per_mapping) + ",\n" +
        kv_int("max_sessions_per_agent", cfg.max_sessions_per_agent) + ",\n" +
        kv_int("max_session_queue_bytes", cfg.max_session_queue_bytes) + ",\n" +
        kv_int("max_tunnel_queue_bytes", cfg.max_tunnel_queue_bytes));
    auto logging = json_obj(2,
        kv_str("level", cfg.log_level) + ",\n" +
        kv_int("max_file_bytes", cfg.log_max_file_bytes) + ",\n" +
        kv_int("retained_files", cfg.log_retained_files));

    std::string body =
        kv_int("schema_version", cfg.schema_version) + ",\n" +
        kv("agent_listener", al) + ",\n" +
        kv("mapping_listener", ml) + ",\n" +
        kv("heartbeat", hb) + ",\n" +
        kv("limits", limits) + ",\n" +
        kv("logging", logging);
    std::string json_text = json_obj(0, body) + "\n";

    auto wr = atomic_write_file(path, json_text);
    if (!wr) return ConfigSaveError{path, wr.error.reason};
    return true;
}

// ===== Devices =====

LoadResult<DevicesConfig> load_devices_config(const std::string& path) {
    DevicesConfig cfg;
    std::string err_str;
    std::string text = read_file(path, err_str);
    if (text.empty() && !err_str.empty()) {
        return ConfigLoadError{path, err_str};
    }

    JsonValue root;
    ConfigLoadError load_err;
    if (!parse_and_validate(path, ConfigFileKind::Devices, text, root, load_err)) {
        return load_err;
    }

    read_int(root, "schema_version", cfg.schema_version);
    auto* devices = root.find("devices");
    if (devices && devices->is_array()) {
        for (std::size_t i = 0; i < devices->size(); ++i) {
            auto* d = devices->at(i);
            if (!d) continue;
            DeviceRecord rec;
            read_string(*d, "id", rec.id);
            read_string(*d, "display_name", rec.display_name);
            read_bool(*d, "enabled", rec.enabled);
            read_string(*d, "device_key_dpapi", rec.device_key_dpapi);
            read_long_long(*d, "created_unix_ms", rec.created_unix_ms);
            read_long_long(*d, "updated_unix_ms", rec.updated_unix_ms);
            cfg.devices.push_back(std::move(rec));
        }
    }
    return cfg;
}

SaveResult save_devices_config(const std::string& path,
                                const DevicesConfig& cfg) {
    std::string devs;
    for (std::size_t i = 0; i < cfg.devices.size(); ++i) {
        auto& d = cfg.devices[i];
        auto obj = json_obj(6,
            kv_str("id", d.id) + ",\n" +
            kv_str("display_name", d.display_name) + ",\n" +
            kv_bool("enabled", d.enabled) + ",\n" +
            kv_str("device_key_dpapi", d.device_key_dpapi) + ",\n" +
            kv_long_long("created_unix_ms", d.created_unix_ms) + ",\n" +
            kv_long_long("updated_unix_ms", d.updated_unix_ms));
        if (i > 0) devs += ",\n";
        devs += obj;
    }
    std::string body =
        kv_int("schema_version", cfg.schema_version) + ",\n" +
        kv("devices", json_arr(2, devs));
    std::string json_text = json_obj(0, body) + "\n";

    auto wr = atomic_write_file(path, json_text);
    if (!wr) return ConfigSaveError{path, wr.error.reason};
    return true;
}

// ===== Mappings =====

LoadResult<MappingsConfig> load_mappings_config(const std::string& path) {
    MappingsConfig cfg;
    std::string err_str;
    std::string text = read_file(path, err_str);
    if (text.empty() && !err_str.empty()) {
        return ConfigLoadError{path, err_str};
    }

    JsonValue root;
    ConfigLoadError load_err;
    if (!parse_and_validate(path, ConfigFileKind::Mappings, text, root, load_err)) {
        return load_err;
    }

    read_int(root, "schema_version", cfg.schema_version);
    auto* mappings = root.find("mappings");
    if (mappings && mappings->is_array()) {
        for (std::size_t i = 0; i < mappings->size(); ++i) {
            auto* m = mappings->at(i);
            if (!m) continue;
            MappingRecord rec;
            read_string(*m, "id", rec.id);
            read_string(*m, "device_id", rec.device_id);
            read_string(*m, "name", rec.name);
            read_int   (*m, "local_port", rec.local_port);
            read_string(*m, "target_host", rec.target_host);
            read_int   (*m, "target_port", rec.target_port);
            read_bool  (*m, "enabled", rec.enabled);
            read_int   (*m, "connect_timeout_ms", rec.connect_timeout_ms);
            cfg.mappings.push_back(std::move(rec));
        }
    }
    return cfg;
}

SaveResult save_mappings_config(const std::string& path,
                                 const MappingsConfig& cfg) {
    std::string maps;
    for (std::size_t i = 0; i < cfg.mappings.size(); ++i) {
        auto& m = cfg.mappings[i];
        auto obj = json_obj(6,
            kv_str("id", m.id) + ",\n" +
            kv_str("device_id", m.device_id) + ",\n" +
            kv_str("name", m.name) + ",\n" +
            kv_int("local_port", m.local_port) + ",\n" +
            kv_str("target_host", m.target_host) + ",\n" +
            kv_int("target_port", m.target_port) + ",\n" +
            kv_bool("enabled", m.enabled) + ",\n" +
            kv_int("connect_timeout_ms", m.connect_timeout_ms));
        if (i > 0) maps += ",\n";
        maps += obj;
    }
    std::string body =
        kv_int("schema_version", cfg.schema_version) + ",\n" +
        kv("mappings", json_arr(2, maps));
    std::string json_text = json_obj(0, body) + "\n";

    auto wr = atomic_write_file(path, json_text);
    if (!wr) return ConfigSaveError{path, wr.error.reason};
    return true;
}

// ===== Agent =====

LoadResult<AgentConfigFile> load_agent_config(const std::string& path) {
    AgentConfigFile cfg;
    std::string err_str;
    std::string text = read_file(path, err_str);
    if (text.empty() && !err_str.empty()) {
        return ConfigLoadError{path, err_str};
    }

    JsonValue root;
    ConfigLoadError load_err;
    if (!parse_and_validate(path, ConfigFileKind::Agent, text, root, load_err)) {
        return load_err;
    }

    read_int(root, "schema_version", cfg.schema_version);

    auto* sv = root.find("server");
    if (sv) { read_string(*sv, "host", cfg.server_host); read_int(*sv, "port", cfg.server_port); }

    auto* dv = root.find("device");
    if (dv) {
        read_string(*dv, "id", cfg.device_id);
        read_string(*dv, "pairing_code", cfg.pairing_code);
        read_string(*dv, "device_key_dpapi", cfg.device_key_dpapi);
    }

    auto* tp = root.find("target_policy");
    if (tp) {
        read_string_array(*tp, "allowed_cidrs", cfg.allowed_cidrs);
        read_int_array(*tp, "allowed_ports", cfg.allowed_ports);
        read_bool(*tp, "allow_ipv6", cfg.allow_ipv6);
    }

    auto* lg = root.find("logging");
    if (lg) {
        read_string(*lg, "level", cfg.log_level);
        read_int(*lg, "max_file_bytes", cfg.log_max_file_bytes);
        read_int(*lg, "retained_files", cfg.log_retained_files);
    }

    return cfg;
}

SaveResult save_agent_config(const std::string& path,
                              const AgentConfigFile& cfg) {
    auto server = json_obj(2,
        kv_str("host", cfg.server_host) + ",\n" +
        kv_int("port", cfg.server_port));
    auto device = json_obj(2,
        kv_str("id", cfg.device_id) + ",\n" +
        kv_str("pairing_code", cfg.pairing_code) + ",\n" +
        kv_str("device_key_dpapi", cfg.device_key_dpapi));

    std::string cidr_arr;
    for (std::size_t i = 0; i < cfg.allowed_cidrs.size(); ++i) {
        if (i > 0) cidr_arr += ",\n";
        cidr_arr += std::string(6, ' ') + make_json_string(cfg.allowed_cidrs[i]);
    }
    std::string port_arr;
    for (std::size_t i = 0; i < cfg.allowed_ports.size(); ++i) {
        if (i > 0) port_arr += ",\n";
        port_arr += std::string(6, ' ') + std::to_string(cfg.allowed_ports[i]);
    }
    auto target_policy = json_obj(4,
        kv("allowed_cidrs", json_arr(4, cidr_arr)) + ",\n" +
        kv("allowed_ports", json_arr(4, port_arr)) + ",\n" +
        kv_bool("allow_ipv6", cfg.allow_ipv6));
    auto logging = json_obj(2,
        kv_str("level", cfg.log_level) + ",\n" +
        kv_int("max_file_bytes", cfg.log_max_file_bytes) + ",\n" +
        kv_int("retained_files", cfg.log_retained_files));

    std::string body =
        kv_int("schema_version", cfg.schema_version) + ",\n" +
        kv("server", server) + ",\n" +
        kv("device", device) + ",\n" +
        kv("target_policy", target_policy) + ",\n" +
        kv("logging", logging);
    std::string json_text = json_obj(0, body) + "\n";

    auto wr = atomic_write_file(path, json_text);
    if (!wr) return ConfigSaveError{path, wr.error.reason};
    return true;
}

}  // namespace rmt::config
