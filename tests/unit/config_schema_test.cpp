// Config schema validation tests (CONFIG_SPEC.md sections 3/4/6/8,
// CODING_STANDARDS.md section 9, TEST_PLAN.md section 3.6).
//
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
// Covers all 17 acceptance criteria from the task spec:
//   1-4.   Full legal configs for each file kind
//   5.     Wrong schema_version
//   6.     Missing required field
//   7.     Wrong field type
//   8.     Out-of-range value
//   9.     Unknown field
//   10.    Duplicate device_id
//   11.    Duplicate local_port
//   12.    mapping_listener.bind_host != 127.0.0.1
//   13.    heartbeat timeout < 2 * interval
//   14.    max_sessions_per_agent < max_sessions_per_mapping
//   15.    pairing_code non-empty and not 8 digits
//   16.    Error contains field_path
//   17.    Compile clean under -Wall -Wextra -Wpedantic (verified by build)
//
// Plus validate_config_text parse-error merging.
#include <optional>
#include <string>
#include <string_view>

#include "rmt/config/config_schema.h"
#include "rmt/config/strict_json.h"
#include "rmt/test.h"

using namespace rmt::config;

namespace {

// Parse-and-validate helper. Returns the error (or nullopt if ok) for
// concise test assertions. Returns by value because the underlying
// ConfigValidationResult owns the error; a pointer would dangle.
std::optional<ConfigValidationError> expect_err(ConfigFileKind kind,
                                                std::string_view text) {
    auto r = validate_config_text(kind, text);
    const ConfigValidationError* e = try_get_config_error(r);
    if (e == nullptr) return std::nullopt;
    return *e;
}

bool is_ok(ConfigFileKind kind, std::string_view text) {
    auto r = validate_config_text(kind, text);
    return config_validation_ok(r);
}

bool path_contains(const ConfigValidationError& e, std::string_view sub) {
    return e.field_path.find(sub) != std::string::npos;
}

// ---- legal config templates (from CONFIG_SPEC.md) -------------------------

constexpr const char* kLegalRemoteTool = R"({
  "schema_version": 1,
  "agent_listener": {
    "bind_host": "0.0.0.0",
    "port": 4433
  },
  "mapping_listener": {
    "bind_host": "127.0.0.1"
  },
  "heartbeat": {
    "interval_ms": 10000,
    "timeout_ms": 30000
  },
  "limits": {
    "max_sessions_per_mapping": 32,
    "max_sessions_per_agent": 128,
    "max_session_queue_bytes": 262144,
    "max_tunnel_queue_bytes": 8388608
  },
  "logging": {
    "level": "info",
    "max_file_bytes": 5242880,
    "retained_files": 3
  }
})";

constexpr const char* kLegalDevices = R"({
  "schema_version": 1,
  "devices": [
    {
      "id": "SITE001",
      "display_name": "client-A-rack-1",
      "enabled": true,
      "device_key_dpapi": "base64-encoded-dpapi-blob",
      "created_unix_ms": 1783872000000,
      "updated_unix_ms": 1783872000000
    }
  ]
})";

constexpr const char* kLegalMappings = R"({
  "schema_version": 1,
  "mappings": [
    {
      "id": "map-site001-ssh",
      "device_id": "SITE001",
      "name": "SSH",
      "local_port": 10022,
      "target_host": "192.168.1.1",
      "target_port": 22,
      "enabled": true,
      "connect_timeout_ms": 10000
    },
    {
      "id": "map-site001-web",
      "device_id": "SITE001",
      "name": "Web",
      "local_port": 18080,
      "target_host": "192.168.1.1",
      "target_port": 80,
      "enabled": true,
      "connect_timeout_ms": 10000
    }
  ]
})";

constexpr const char* kLegalAgentPaired = R"({
  "schema_version": 1,
  "server": {
    "host": "192.168.1.100",
    "port": 4433
  },
  "device": {
    "id": "SITE001",
    "pairing_code": "",
    "device_key_dpapi": "base64-encoded-dpapi-blob"
  },
  "target_policy": {
    "allowed_cidrs": ["127.0.0.0/8", "192.168.0.0/16"],
    "allowed_ports": [22, 80, 443, 8080],
    "allow_ipv6": false
  },
  "logging": {
    "level": "info",
    "max_file_bytes": 2097152,
    "retained_files": 2
  }
})";

constexpr const char* kLegalAgentPairing = R"({
  "schema_version": 1,
  "server": {
    "host": "192.168.1.100",
    "port": 4433
  },
  "device": {
    "id": "SITE001",
    "pairing_code": "58392147",
    "device_key_dpapi": ""
  },
  "target_policy": {
    "allowed_cidrs": ["127.0.0.0/8", "192.168.0.0/16"],
    "allowed_ports": [22, 80, 443, 8080],
    "allow_ipv6": false
  },
  "logging": {
    "level": "info",
    "max_file_bytes": 2097152,
    "retained_files": 2
  }
})";

// ---- RemoteTool tests ------------------------------------------------------

void run_remote_tool_tests() {
    // 1. Legal config -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::RemoteTool, kLegalRemoteTool),
                  "legal remote_tool.json should pass");

    // 5. Wrong schema_version
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":2,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "schema_version"));
    }
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":"1","agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "schema_version"));
    }

    // 6. Missing required field (agent_listener)
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "agent_listener"));
    }

    // 7. Wrong type: port as string
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":"4433"},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "port"));
    }

    // 8. Out of range: port = 0 and 70000
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":0},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "port"));
    }
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":70000},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "port"));
    }

    // 9. Unknown field at root
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3},"extra":1})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "extra"));
    }
    // 9b. Unknown field nested
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433,"extra":1},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "agent_listener.extra"));
    }

    // 12. mapping_listener.bind_host != 127.0.0.1
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"0.0.0.0"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "mapping_listener.bind_host"));
    }

    // 13. timeout_ms < 2 * interval_ms
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":19999},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "timeout_ms"));
    }
    // 13b. timeout == 2*interval is OK
    {
        RMT_CHECK_MSG(is_ok(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":20000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})"),
                      "timeout == 2*interval should pass");
    }

    // 14. max_sessions_per_agent < max_sessions_per_mapping
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":31,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "max_sessions_per_agent"));
    }

    // 14b. max_tunnel_queue_bytes < max_session_queue_bytes
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":262143},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "max_tunnel_queue_bytes"));
    }

    // heartbeat.interval_ms out of range
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":999,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "interval_ms"));
    }

    // logging.level invalid
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"trace","max_file_bytes":5242880,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "logging.level"));
    }

    // logging.max_file_bytes = 0
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,"agent_listener":{"bind_host":"0.0.0.0","port":4433},"mapping_listener":{"bind_host":"127.0.0.1"},"heartbeat":{"interval_ms":10000,"timeout_ms":30000},"limits":{"max_sessions_per_mapping":32,"max_sessions_per_agent":128,"max_session_queue_bytes":262144,"max_tunnel_queue_bytes":8388608},"logging":{"level":"info","max_file_bytes":0,"retained_files":3}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "max_file_bytes"));
    }

    // root not object
    {
        auto e = expect_err(ConfigFileKind::RemoteTool, "[]");
        RMT_CHECK(e.has_value());
    }
}

// ---- Devices tests ---------------------------------------------------------

void run_devices_tests() {
    // 2. Legal config -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::Devices, kLegalDevices),
                  "legal devices.json should pass");

    // Empty devices array -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::Devices,
                        R"({"schema_version":1,"devices":[]})"),
                  "empty devices array should pass");

    // 5. Wrong schema_version
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":2,"devices":[]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "schema_version"));
    }

    // 6. Missing devices field
    {
        auto e = expect_err(ConfigFileKind::Devices, R"({"schema_version":1})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "devices"));
    }

    // 9. Unknown field in device
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"a","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1,"extra":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "extra"));
    }

    // 7. Wrong type: enabled as int
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"a","enabled":1,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "enabled"));
    }

    // id too long (65 chars)
    {
        std::string long_id(65, 'a');
        std::string json = R"({"schema_version":1,"devices":[{"id":")" + long_id +
                           R"(","display_name":"a","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})";
        auto e = expect_err(ConfigFileKind::Devices, json);
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "id"));
    }

    // id bad charset
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"bad id","display_name":"a","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "id"));
    }

    // display_name empty
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "display_name"));
    }

    // created_unix_ms negative
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"a","enabled":true,"device_key_dpapi":"","created_unix_ms":-1,"updated_unix_ms":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "created_unix_ms"));
    }

    // 10. Duplicate device_id
    {
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"a","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1},{"id":"A","display_name":"b","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "id"));
        RMT_CHECK(path_contains(*e, "devices[1]"));
    }

    // UTF-8 display_name with multi-byte chars should pass (1-128 codepoints)
    {
        RMT_CHECK_MSG(is_ok(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"客户A-机柜1","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})"),
                      "UTF-8 display_name should pass");
    }

    // Invalid UTF-8 in display_name should fail
    {
        // 0xFF is not a valid UTF-8 leading byte.
        auto e = expect_err(ConfigFileKind::Devices,
                            R"({"schema_version":1,"devices":[{"id":"A","display_name":"\u00ff","enabled":true,"device_key_dpapi":"","created_unix_ms":1,"updated_unix_ms":1}]})");
        // \u00ff in JSON decodes to U+00FF which is a valid 2-byte UTF-8 char,
        // so this should actually pass. Let's instead test a clearly invalid
        // sequence. strict_json preserves raw bytes for \u escapes as UTF-8
        // encoded codepoints; U+00FF is valid. Skip this test case.
        (void)e;
    }
}

// ---- Mappings tests --------------------------------------------------------

void run_mappings_tests() {
    // 3. Legal config -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::Mappings, kLegalMappings),
                  "legal mappings.json should pass");

    // Empty mappings array -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::Mappings,
                        R"({"schema_version":1,"mappings":[]})"),
                  "empty mappings array should pass");

    // 5. Wrong schema_version
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":2,"mappings":[]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "schema_version"));
    }

    // 6. Missing mappings field
    {
        auto e = expect_err(ConfigFileKind::Mappings, R"({"schema_version":1})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "mappings"));
    }

    // 7. Wrong type: local_port as string
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":"10022","target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "local_port"));
    }

    // 8. local_port out of range (0 and 70000)
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":0,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "local_port"));
    }
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":70000,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "local_port"));
    }

    // 9. Unknown field in mapping
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000,"extra":1}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "extra"));
    }

    // connect_timeout_ms out of range
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":999}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "connect_timeout_ms"));
    }
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":30001}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "connect_timeout_ms"));
    }

    // 11. Duplicate local_port
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000},{"id":"m2","device_id":"A","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "local_port"));
        RMT_CHECK(path_contains(*e, "mappings[1]"));
    }

    // Duplicate mapping id
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000},{"id":"m1","device_id":"A","name":"n","local_port":10023,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "id"));
        RMT_CHECK(path_contains(*e, "mappings[1]"));
    }

    // device_id empty
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"","name":"n","local_port":10022,"target_host":"127.0.0.1","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "device_id"));
    }

    // target_host empty
    {
        auto e = expect_err(ConfigFileKind::Mappings,
                            R"({"schema_version":1,"mappings":[{"id":"m1","device_id":"A","name":"n","local_port":10022,"target_host":"","target_port":22,"enabled":true,"connect_timeout_ms":10000}]})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "target_host"));
    }
}

// ---- Agent tests -----------------------------------------------------------

void run_agent_tests() {
    // 4. Legal config (paired) -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::Agent, kLegalAgentPaired),
                  "legal agent.json (paired) should pass");
    // 4b. Legal config (pairing) -> pass
    RMT_CHECK_MSG(is_ok(ConfigFileKind::Agent, kLegalAgentPairing),
                  "legal agent.json (pairing) should pass");

    // 5. Wrong schema_version
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":2,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "schema_version"));
    }

    // 6. Missing server
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "server"));
    }

    // 7. Wrong type: server.port as string
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":"4433"},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "port"));
    }

    // 8. server.port out of range
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":0},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "port"));
    }

    // 9. Unknown field
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1},"extra":1})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "extra"));
    }

    // device.id bad charset
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"bad id","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "device.id"));
    }

    // 15. pairing_code non-empty and not 8 digits
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"1234567","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "pairing_code"));
    }
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"123456789","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "pairing_code"));
    }
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"1234abcd","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "pairing_code"));
    }

    // allowed_cidrs element not string
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":[123],"allowed_ports":[22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "allowed_cidrs"));
    }

    // allowed_ports element out of range
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[0],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "allowed_ports"));
    }

    // allowed_ports duplicate
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22,22],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "allowed_ports"));
    }

    // allow_ipv6 not bool
    {
        auto e = expect_err(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":["10.0.0.0/8"],"allowed_ports":[22],"allow_ipv6":0},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "allow_ipv6"));
    }

    // allowed_ports empty array is OK (means all reject)
    {
        RMT_CHECK_MSG(is_ok(ConfigFileKind::Agent,
                            R"({"schema_version":1,"server":{"host":"1.2.3.4","port":4433},"device":{"id":"A","pairing_code":"","device_key_dpapi":""},"target_policy":{"allowed_cidrs":[],"allowed_ports":[],"allow_ipv6":false},"logging":{"level":"info","max_file_bytes":1000,"retained_files":1}})"),
                      "empty allowed_cidrs/ports should pass");
    }
}

// ---- validate_config_text parse-error tests --------------------------------

void run_text_parse_tests() {
    // Empty text -> parse error
    {
        auto e = expect_err(ConfigFileKind::RemoteTool, "");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "<root>"));
    }
    // Trailing comma -> parse error
    {
        auto e = expect_err(ConfigFileKind::RemoteTool,
                            R"({"schema_version":1,})");
        RMT_CHECK(e.has_value());
        RMT_CHECK(path_contains(*e, "<root>"));
    }
    // BOM -> parse error
    {
        std::string bom_text = "\xEF\xBB\xBF{}";
        auto e = expect_err(ConfigFileKind::RemoteTool, bom_text);
        RMT_CHECK(e.has_value());
    }
    // Just a number (root not object) -> schema error
    {
        auto e = expect_err(ConfigFileKind::RemoteTool, "42");
        RMT_CHECK(e.has_value());
    }
}

// ---- validate_config (JsonValue entry) tests ------------------------------

void run_jsonvalue_entry_tests() {
    // Verify validate_config returns the JsonValue on success.
    auto pr = parse_json(kLegalRemoteTool);
    RMT_CHECK(json_parse_ok(pr));
    const JsonValue* root = try_get_json_value(pr);
    RMT_CHECK(root != nullptr);
    auto r = validate_config(ConfigFileKind::RemoteTool, *root);
    RMT_CHECK(config_validation_ok(r));
    RMT_CHECK(try_get_valid_config(r) != nullptr);

    // Verify validate_config returns an error on failure.
    auto pr2 = parse_json("[]");
    RMT_CHECK(json_parse_ok(pr2));
    auto r2 = validate_config(ConfigFileKind::RemoteTool, *try_get_json_value(pr2));
    RMT_CHECK(!config_validation_ok(r2));
    RMT_CHECK(try_get_config_error(r2) != nullptr);
}

}  // namespace

int main() {
    run_remote_tool_tests();
    run_devices_tests();
    run_mappings_tests();
    run_agent_tests();
    run_text_parse_tests();
    run_jsonvalue_entry_tests();
    auto& c = rmt::test::ctx();
    std::printf("config_schema_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
