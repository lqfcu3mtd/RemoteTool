// ConfigLoader unit tests — round-trip load/save for all 4 config types.
#include <cstdio>
#include <filesystem>
#include <string>

#include "rmt/config/atomic_write.h"
#include "rmt/config/config_loader.h"
#include "rmt/test.h"

using namespace rmt::config;
namespace fs = std::filesystem;

namespace {

fs::path tmpdir() {
    auto p = fs::temp_directory_path() / "rmt_cfg_test";
    fs::create_directories(p);
    return p;
}

void cleanup(const fs::path& d) { fs::remove_all(d); }

// ===== RemoteTool round-trip =====
void test_remote_tool_roundtrip() {
    auto d = tmpdir();
    auto path = (d / "remote_tool.json").string();

    RemoteToolConfig cfg;
    cfg.agent_port = 12345;
    cfg.heartbeat_interval_ms = 5000;
    cfg.max_sessions_per_mapping = 10;

    auto sr = save_remote_tool_config(path, cfg);
    RMT_CHECK_MSG(save_ok(sr), "save failed");

    auto lr = load_remote_tool_config(path);
    RMT_CHECK_MSG(load_ok(lr), "load failed");
    auto* loaded = try_get_loaded(lr);
    RMT_CHECK(loaded->agent_port == 12345);
    RMT_CHECK(loaded->heartbeat_interval_ms == 5000);
    RMT_CHECK(loaded->max_sessions_per_mapping == 10);
    RMT_CHECK(loaded->bind_host == "0.0.0.0");  // default

    cleanup(d);
}

// ===== Devices round-trip =====
void test_devices_roundtrip() {
    auto d = tmpdir();
    auto path = (d / "devices.json").string();

    DevicesConfig cfg;
    DeviceRecord rec;
    rec.id = "DEV01";
    rec.display_name = "test-device";
    rec.enabled = true;
    rec.device_key_dpapi = "BASE64BLOB==";
    cfg.devices.push_back(rec);

    auto sr = save_devices_config(path, cfg);
    RMT_CHECK_MSG(save_ok(sr), "save failed");

    auto lr = load_devices_config(path);
    RMT_CHECK_MSG(load_ok(lr), "load failed");
    auto* loaded = try_get_loaded(lr);
    RMT_CHECK(loaded->devices.size() == 1);
    RMT_CHECK(loaded->devices[0].id == "DEV01");
    RMT_CHECK(loaded->devices[0].device_key_dpapi == "BASE64BLOB==");

    cleanup(d);
}

// ===== Mappings round-trip =====
void test_mappings_roundtrip() {
    auto d = tmpdir();
    auto path = (d / "mappings.json").string();

    MappingsConfig cfg;
    MappingRecord m;
    m.id = "map-ssh";
    m.device_id = "DEV01";
    m.name = "SSH";
    m.local_port = 10022;
    m.target_host = "192.168.1.1";
    m.target_port = 22;
    cfg.mappings.push_back(m);

    auto sr = save_mappings_config(path, cfg);
    RMT_CHECK_MSG(save_ok(sr), "save failed");

    auto lr = load_mappings_config(path);
    RMT_CHECK_MSG(load_ok(lr), "load failed");
    auto* loaded = try_get_loaded(lr);
    RMT_CHECK(loaded->mappings.size() == 1);
    RMT_CHECK(loaded->mappings[0].local_port == 10022);
    RMT_CHECK(loaded->mappings[0].target_host == "192.168.1.1");

    cleanup(d);
}

// ===== Agent round-trip =====
void test_agent_roundtrip() {
    auto d = tmpdir();
    auto path = (d / "agent.json").string();

    AgentConfigFile cfg;
    cfg.server_host = "10.0.0.1";
    cfg.server_port = 4433;
    cfg.device_id = "AGENT01";
    cfg.pairing_code = "12345678";
    cfg.allowed_cidrs = {"127.0.0.0/8", "192.168.0.0/16"};
    cfg.allowed_ports = {22, 80, 443};

    auto sr = save_agent_config(path, cfg);
    RMT_CHECK_MSG(save_ok(sr), "save failed");

    auto lr = load_agent_config(path);
    RMT_CHECK_MSG(load_ok(lr), "load failed");
    auto* loaded = try_get_loaded(lr);
    RMT_CHECK(loaded->server_host == "10.0.0.1");
    RMT_CHECK(loaded->device_id == "AGENT01");
    RMT_CHECK(loaded->allowed_cidrs.size() == 2);
    RMT_CHECK(loaded->allowed_ports.size() == 3);

    cleanup(d);
}

// ===== Load missing file =====
void test_load_missing_file() {
    auto lr = load_remote_tool_config("/nonexistent/rmt_cfg_test.json");
    RMT_CHECK_MSG(!load_ok(lr), "should fail on missing file");
}

// ===== Load invalid JSON =====
void test_load_invalid_json() {
    auto d = tmpdir();
    auto path = (d / "bad.json").string();
    atomic_write_file(path, "not json {{{");
    auto lr = load_remote_tool_config(path);
    RMT_CHECK_MSG(!load_ok(lr), "should fail on invalid JSON");
    cleanup(d);
}

}  // namespace

int main() {
    test_remote_tool_roundtrip();
    test_devices_roundtrip();
    test_mappings_roundtrip();
    test_agent_roundtrip();
    test_load_missing_file();
    test_load_invalid_json();

    auto& ctx = rmt::test::ctx();
    std::printf("config_loader_test: %d passed, %d failed\n",
                ctx.passed, ctx.failed);
    return ctx.ok() ? 0 : 1;
}
