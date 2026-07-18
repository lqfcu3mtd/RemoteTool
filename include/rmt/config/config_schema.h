#pragma once
// Schema validation for RemoteTool config files (CONFIG_SPEC.md sections
// 3/4/6/8, CODING_STANDARDS.md section 9, TEST_PLAN.md section 3.6).
//
// This module operates on a successfully-parsed JsonValue tree (see
// strict_json.h). It performs field-level semantic validation: required
// fields, type checks, range checks, cross-field relationships, unknown
// field rejection, and uniqueness checks. It does NOT do file I/O (that is
// the responsibility of atomic_write + the caller) and does NOT auto-repair
// or silently drop unknown fields (CONFIG_SPEC.md section 1: "配置损坏时不得
// 自动覆盖或恢复默认值").
//
// On success the input JsonValue is returned to the caller for downstream
// consumption (copied by value; config files are small). On failure a
// ConfigValidationError with a dotted field path (e.g.
// "limits.max_sessions_per_mapping") and a human-readable reason is
// returned. Validation is fail-fast: the first error stops validation,
// matching the strict_json convention.
//
// Cross-file reference checks (e.g. mappings.json device_id must exist in
// devices.json) are intentionally NOT performed here; they require multiple
// loaded files and are the caller's responsibility. The schema layer only
// checks the format of each field in isolation.
#include <string>
#include <string_view>
#include <variant>

#include "rmt/config/strict_json.h"

namespace rmt::config {

enum class ConfigFileKind {
    RemoteTool,  // remote_tool.json (CONFIG_SPEC.md section 3)
    Devices,     // devices.json    (CONFIG_SPEC.md section 4)
    Mappings,    // mappings.json   (CONFIG_SPEC.md section 6)
    Agent,       // agent.json      (CONFIG_SPEC.md section 8)
};

struct ConfigValidationError {
    // Dotted field path within the document. Examples:
    //   "schema_version"
    //   "agent_listener.port"
    //   "devices[2].id"
    //   "mappings[0].local_port"
    //   ""  (empty, for root-level errors such as "root must be an object")
    std::string field_path;

    // Human-readable explanation. Diagnostic text only; not a stable error
    // code. Callers must not programmatically parse this.
    std::string reason;
};

// Success -> the validated JsonValue (copied; safe for downstream use).
// Failure -> ConfigValidationError (first error encountered, fail-fast).
using ConfigValidationResult = std::variant<JsonValue, ConfigValidationError>;

inline bool config_validation_ok(const ConfigValidationResult& r) noexcept {
    return std::holds_alternative<JsonValue>(r);
}
inline const JsonValue* try_get_valid_config(const ConfigValidationResult& r) noexcept {
    return std::get_if<JsonValue>(&r);
}
inline const ConfigValidationError* try_get_config_error(
    const ConfigValidationResult& r) noexcept {
    return std::get_if<ConfigValidationError>(&r);
}

// Validate an already-parsed JsonValue tree against the schema for the given
// config file kind. Fail-fast: returns the first error encountered.
ConfigValidationResult validate_config(ConfigFileKind kind, const JsonValue& root);

// Convenience: parse `text` with strict_json and validate the result. If the
// parse fails, returns a ConfigValidationError wrapping the parse error
// (field_path = "<root>"). This helper is the typical entry point for
// config loaders that have the raw file text in hand.
ConfigValidationResult validate_config_text(ConfigFileKind kind, std::string_view text);

}  // namespace rmt::config
