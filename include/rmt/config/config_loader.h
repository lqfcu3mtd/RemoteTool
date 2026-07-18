#pragma once
// ConfigLoader — load and save JSON config files with schema validation.
// Uses strict_json (parse) + config_schema (validate) + atomic_write (save).
// CONFIG_SPEC.md sections 3/4/6/8.
//
// Each load_xxx_config() reads a file, parses JSON, validates against the
// corresponding schema, and returns a typed config struct. On any failure
// the original file is untouched and a ConfigLoadError is returned.
//
// Each save_xxx_config() serialises the struct to JSON and atomically writes
// it to disk. On failure the original file is not damaged.
//
// Phase 4: DPAPI-protected device_key is handled by the caller (SecretStore)
//           before saving / after loading. This module only handles JSON
//           serialisation of the device_key_dpapi string field.

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rmt::config {

// ===== error types =====

struct ConfigLoadError {
    std::string path;     // file path
    std::string reason;   // human-readable error
};

template <typename T>
using LoadResult = std::variant<T, ConfigLoadError>;

struct ConfigSaveError {
    std::string path;
    std::string reason;
};

using SaveResult = std::variant<bool, ConfigSaveError>;  // true = success

// helpers
template <typename T>
inline bool load_ok(const LoadResult<T>& r) noexcept { return std::holds_alternative<T>(r); }
template <typename T>
inline const T* try_get_loaded(const LoadResult<T>& r) noexcept { return std::get_if<T>(&r); }
inline bool save_ok(const SaveResult& r) noexcept { return std::holds_alternative<bool>(r); }

// ===== config structs =====

// remote_tool.json  (CONFIG_SPEC section 3)
struct RemoteToolConfig {
    int schema_version = 1;
    std::string bind_host = "0.0.0.0";
    int agent_port = 4433;
    std::string mapping_bind_host = "127.0.0.1";
    int heartbeat_interval_ms = 10000;
    int heartbeat_timeout_ms = 30000;
    int max_sessions_per_mapping = 32;
    int max_sessions_per_agent = 128;
    int max_session_queue_bytes = 262144;
    int max_tunnel_queue_bytes = 8388608;
    std::string log_level = "info";
    int log_max_file_bytes = 5242880;
    int log_retained_files = 3;
};

// devices.json  (CONFIG_SPEC section 4)
struct DeviceRecord {
    std::string id;
    std::string display_name;
    bool enabled = true;
    std::string device_key_dpapi;   // Base64-encoded DPAPI blob, empty = pending
    long long created_unix_ms = 0;
    long long updated_unix_ms = 0;
};

struct DevicesConfig {
    int schema_version = 1;
    std::vector<DeviceRecord> devices;
};

// mappings.json  (CONFIG_SPEC section 6)
struct MappingRecord {
    std::string id;
    std::string device_id;
    std::string name;
    int local_port = 0;
    std::string target_host;
    int target_port = 0;
    bool enabled = true;
    int connect_timeout_ms = 10000;
};

struct MappingsConfig {
    int schema_version = 1;
    std::vector<MappingRecord> mappings;
};

// agent.json  (CONFIG_SPEC section 8)
struct AgentConfigFile {
    int schema_version = 1;
    std::string server_host;
    int server_port = 0;
    std::string device_id;
    std::string pairing_code;       // empty or 8 digits
    std::string device_key_dpapi;   // empty = unprovisioned
    // target_policy
    std::vector<std::string> allowed_cidrs;
    std::vector<int> allowed_ports;
    bool allow_ipv6 = false;
    // logging
    std::string log_level = "info";
    int log_max_file_bytes = 2097152;
    int log_retained_files = 2;
};

// ===== load / save =====

LoadResult<RemoteToolConfig> load_remote_tool_config(const std::string& path);
SaveResult save_remote_tool_config(const std::string& path, const RemoteToolConfig& cfg);

LoadResult<DevicesConfig> load_devices_config(const std::string& path);
SaveResult save_devices_config(const std::string& path, const DevicesConfig& cfg);

LoadResult<MappingsConfig> load_mappings_config(const std::string& path);
SaveResult save_mappings_config(const std::string& path, const MappingsConfig& cfg);

LoadResult<AgentConfigFile> load_agent_config(const std::string& path);
SaveResult save_agent_config(const std::string& path, const AgentConfigFile& cfg);

}  // namespace rmt::config
