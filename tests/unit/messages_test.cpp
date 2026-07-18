// Protocol message codec tests (PROTOCOL_SPEC.md section 6.1-6.6).
// Compiles under MinGW or MSVC.
#include <cstdint>
#include <string>
#include <vector>

#include "rmt/protocol/frame.h"
#include "rmt/protocol/messages.h"
#include "rmt/test.h"

using namespace rmt::protocol;

namespace {

// Helper: build a Frame with specific type, session, and string payload.
Frame make_frame(MessageType type, SessionId session, const std::string& payload) {
    Frame f;
    f.header.type = type;
    f.header.session_id = session;
    f.payload.assign(payload.begin(), payload.end());
    return f;
}

// Helper: check decode error contains a substring.
bool error_contains(const std::string& err, const std::string& needle) {
    return err.find(needle) != std::string::npos;
}

void run_hello_tests() {
    // --- Test 1: encode_hello + decode_hello round-trip ---
    {
        HelloMessage h;
        h.device_id = "SITE001";
        h.agent_version = "0.1.0";
        h.platform = "windows-x86_64";
        h.protocol_version = 1;
        h.instance_nonce = "dGVzdC1ub25jZQ";

        auto frame = encode_hello(h);
        RMT_CHECK(frame.header.type == MsgHello);
        RMT_CHECK(frame.header.session_id == 0);

        auto result = decode_hello(frame);
        auto* msg = std::get_if<HelloMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode_hello succeeded");
        if (msg) {
            RMT_CHECK(msg->device_id == "SITE001");
            RMT_CHECK(msg->agent_version == "0.1.0");
            RMT_CHECK(msg->platform == "windows-x86_64");
            RMT_CHECK(msg->protocol_version == 1);
            RMT_CHECK(msg->instance_nonce == "dGVzdC1ub25jZQ");
        }
    }

    // --- Empty instance_nonce (valid per spec) ---
    {
        HelloMessage h;
        h.device_id = "AGENT";
        h.agent_version = "1.0";
        h.platform = "linux-x86_64";
        h.protocol_version = 1;
        h.instance_nonce = "";

        auto frame = encode_hello(h);
        auto result = decode_hello(frame);
        auto* msg = std::get_if<HelloMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode_hello with empty nonce");
        if (msg) {
            RMT_CHECK(msg->instance_nonce.empty());
        }
    }

    // --- Device ID edge: valid chars (alphanum, dash, underscore, dot) ---
    {
        HelloMessage h;
        h.device_id = "A-Z_a-z_0-9.Test";
        h.agent_version = "1.0";
        h.platform = "test";
        h.protocol_version = 1;
        h.instance_nonce = "";

        auto frame = encode_hello(h);
        auto result = decode_hello(frame);
        RMT_CHECK_MSG(std::get_if<HelloMessage>(&result) != nullptr,
                      "device_id with valid special chars");
    }

    // --- Device ID too long (>64) -> error ---
    {
        HelloMessage h;
        h.device_id = std::string(65, 'A');
        h.agent_version = "1.0";
        h.platform = "test";
        h.protocol_version = 1;
        h.instance_nonce = "";

        auto frame = encode_hello(h);
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "device_id too long rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "device_id invalid"),
                          "error mentions device_id invalid: " + *err);
        }
    }

    // --- Device ID with invalid char -> error ---
    {
        HelloMessage h;
        h.device_id = "bad@char";
        h.agent_version = "1.0";
        h.platform = "test";
        h.protocol_version = 1;
        h.instance_nonce = "";

        auto frame = encode_hello(h);
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "device_id with @ rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "device_id invalid"),
                          "error mentions device_id invalid: " + *err);
        }
    }

    // --- protocol_version != 1 -> error ---
    {
        std::string payload = "{\"device_id\":\"A\",\"agent_version\":\"1.0\","
                              "\"platform\":\"t\",\"protocol_version\":2,"
                              "\"instance_nonce\":\"\"}";
        auto frame = make_frame(MsgHello, 0, payload);
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "protocol_version=2 rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "protocol_version must be 1"),
                          "error mentions version: " + *err);
        }
    }

    // --- protocol_version as string type -> error ---
    {
        std::string payload = "{\"device_id\":\"A\",\"agent_version\":\"1.0\","
                              "\"platform\":\"t\",\"protocol_version\":\"1\","
                              "\"instance_nonce\":\"\"}";
        auto frame = make_frame(MsgHello, 0, payload);
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "protocol_version as string rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "must be an integer"),
                          "error mentions type: " + *err);
        }
    }

    // --- Non-HELLO frame -> type mismatch error ---
    {
        auto hb_frame = make_frame(MsgHeartbeat, 0,
                                   "{\"sequence\":1,\"sent_unix_ms\":1,"
                                   "\"active_sessions\":0}");
        auto result = decode_hello(hb_frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "non-HELLO frame rejected by decode_hello");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "type mismatch"),
                          "error mentions type: " + *err);
        }
    }

    // --- Invalid JSON payload -> error ---
    {
        auto frame = make_frame(MsgHello, 0, "not json");
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "invalid JSON rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "JSON parse error"),
                          "error mentions JSON: " + *err);
        }
    }

    // --- Payload not an object (JSON array) -> error ---
    {
        auto frame = make_frame(MsgHello, 0, "[1,2,3]");
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "array payload rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "must be a JSON object"),
                          "error mentions object: " + *err);
        }
    }

    // --- Unknown field in payload -> error ---
    {
        std::string payload = "{\"device_id\":\"A\",\"agent_version\":\"1.0\","
                              "\"platform\":\"t\",\"protocol_version\":1,"
                              "\"instance_nonce\":\"\",\"extra_field\":123}";
        auto frame = make_frame(MsgHello, 0, payload);
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "unknown field rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "unknown field"),
                          "error mentions unknown field: " + *err);
        }
    }

    // --- Missing required field -> error ---
    {
        std::string payload = "{\"device_id\":\"A\",\"agent_version\":\"1.0\","
                              "\"protocol_version\":1,"
                              "\"instance_nonce\":\"\"}";
        auto frame = make_frame(MsgHello, 0, payload);
        auto result = decode_hello(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "missing platform rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "platform"),
                          "error mentions platform: " + *err);
        }
    }
}

void run_hello_ack_tests() {
    // --- Test: encode_hello_ack(accepted=true) + decode round-trip ---
    {
        HelloAckMessage ack;
        ack.accepted = true;
        ack.server_version = "0.1.0";
        ack.heartbeat_interval_ms = 10000;
        ack.heartbeat_timeout_ms = 30000;
        ack.max_sessions = 128;

        auto frame = encode_hello_ack(ack);
        RMT_CHECK(frame.header.type == MsgHelloAck);
        RMT_CHECK(frame.header.session_id == 0);

        auto result = decode_hello_ack(frame);
        auto* msg = std::get_if<HelloAckMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode_hello_ack(accepted=true) succeeded");
        if (msg) {
            RMT_CHECK(msg->accepted == true);
            RMT_CHECK(msg->server_version == "0.1.0");
            RMT_CHECK(msg->heartbeat_interval_ms == 10000);
            RMT_CHECK(msg->heartbeat_timeout_ms == 30000);
            RMT_CHECK(msg->max_sessions == 128);
            RMT_CHECK(msg->error_code.empty());
            RMT_CHECK(msg->message.empty());
        }
    }

    // --- Test: encode_hello_ack(accepted=false) + decode ---
    {
        HelloAckMessage ack;
        ack.accepted = false;
        ack.server_version = "0.1.0";
        ack.heartbeat_interval_ms = 10000;
        ack.heartbeat_timeout_ms = 30000;
        ack.max_sessions = 0;
        ack.error_code = "DEVICE_DISABLED";
        ack.message = "device is disabled";

        auto frame = encode_hello_ack(ack);
        auto result = decode_hello_ack(frame);
        auto* msg = std::get_if<HelloAckMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode_hello_ack(accepted=false) succeeded");
        if (msg) {
            RMT_CHECK(msg->accepted == false);
            RMT_CHECK(msg->max_sessions == 0);
            RMT_CHECK(msg->error_code == "DEVICE_DISABLED");
            RMT_CHECK(msg->message == "device is disabled");
        }
    }

    // --- Test: accepted=false without error_code/message -> defaults to "" ---
    {
        std::string payload = "{\"accepted\":false,\"server_version\":\"0.1.0\","
                              "\"heartbeat_interval_ms\":10000,"
                              "\"heartbeat_timeout_ms\":30000,"
                              "\"max_sessions\":0}";
        auto frame = make_frame(MsgHelloAck, 0, payload);
        auto result = decode_hello_ack(frame);
        auto* msg = std::get_if<HelloAckMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode without error_code defaults ok");
        if (msg) {
            RMT_CHECK(msg->error_code.empty());
            RMT_CHECK(msg->message.empty());
        }
    }

    // --- heartbeat_interval_ms < 1000 -> error ---
    {
        std::string payload = "{\"accepted\":true,\"server_version\":\"1.0\","
                              "\"heartbeat_interval_ms\":500,"
                              "\"heartbeat_timeout_ms\":30000,"
                              "\"max_sessions\":128}";
        auto frame = make_frame(MsgHelloAck, 0, payload);
        auto result = decode_hello_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "interval < 1000 rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "heartbeat_interval_ms"),
                          "error mentions interval: " + *err);
        }
    }

    // --- heartbeat_interval_ms > 60000 -> error ---
    {
        std::string payload = "{\"accepted\":true,\"server_version\":\"1.0\","
                              "\"heartbeat_interval_ms\":70000,"
                              "\"heartbeat_timeout_ms\":140000,"
                              "\"max_sessions\":128}";
        auto frame = make_frame(MsgHelloAck, 0, payload);
        auto result = decode_hello_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "interval > 60000 rejected");
    }

    // --- heartbeat_timeout_ms < 2 * interval -> error ---
    {
        std::string payload = "{\"accepted\":true,\"server_version\":\"1.0\","
                              "\"heartbeat_interval_ms\":10000,"
                              "\"heartbeat_timeout_ms\":19999,"
                              "\"max_sessions\":128}";
        auto frame = make_frame(MsgHelloAck, 0, payload);
        auto result = decode_hello_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "timeout < 2*interval rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, ">= 2 * heartbeat_interval_ms"),
                          "error mentions constraint: " + *err);
        }
    }

    // --- heartbeat_timeout_ms > 180000 -> error ---
    {
        std::string payload = "{\"accepted\":true,\"server_version\":\"1.0\","
                              "\"heartbeat_interval_ms\":60000,"
                              "\"heartbeat_timeout_ms\":180001,"
                              "\"max_sessions\":128}";
        auto frame = make_frame(MsgHelloAck, 0, payload);
        auto result = decode_hello_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "timeout > 180000 rejected");
    }

    // --- max_sessions < 0 -> error ---
    {
        std::string payload = "{\"accepted\":true,\"server_version\":\"1.0\","
                              "\"heartbeat_interval_ms\":10000,"
                              "\"heartbeat_timeout_ms\":30000,"
                              "\"max_sessions\":-1}";
        auto frame = make_frame(MsgHelloAck, 0, payload);
        auto result = decode_hello_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "max_sessions < 0 rejected");
    }
}

void run_heartbeat_tests() {
    // --- Test: encode_heartbeat + decode_heartbeat round-trip ---
    {
        HeartbeatMessage hb;
        hb.sequence = 42;
        hb.sent_unix_ms = 1783872000000LL;
        hb.active_sessions = 3;

        auto frame = encode_heartbeat(hb);
        RMT_CHECK(frame.header.type == MsgHeartbeat);
        RMT_CHECK(frame.header.session_id == 0);

        auto result = decode_heartbeat(frame);
        auto* msg = std::get_if<HeartbeatMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode_heartbeat succeeded");
        if (msg) {
            RMT_CHECK(msg->sequence == 42);
            RMT_CHECK(msg->sent_unix_ms == 1783872000000LL);
            RMT_CHECK(msg->active_sessions == 3);
        }
    }

    // --- Negative values -> error ---
    {
        std::string payload = "{\"sequence\":-1,\"sent_unix_ms\":0,"
                              "\"active_sessions\":0}";
        auto frame = make_frame(MsgHeartbeat, 0, payload);
        auto result = decode_heartbeat(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "negative sequence rejected");
        if (err) {
            RMT_CHECK_MSG(error_contains(*err, "sequence"),
                          "error mentions sequence: " + *err);
        }
    }

    // --- Type mismatch ---
    {
        HelloMessage h;
        h.device_id = "A";
        h.agent_version = "1.0";
        h.platform = "t";
        h.protocol_version = 1;
        h.instance_nonce = "";
        auto frame = encode_hello(h);
        auto result = decode_heartbeat(frame);
        RMT_CHECK_MSG(std::get_if<std::string>(&result) != nullptr,
                      "type mismatch rejected by decode_heartbeat");
    }
}

void run_heartbeat_ack_tests() {
    // --- Test: encode_heartbeat_ack + decode round-trip ---
    {
        HeartbeatAckMessage ack;
        ack.sequence = 42;
        ack.received_unix_ms = 1783872000012LL;

        auto frame = encode_heartbeat_ack(ack);
        RMT_CHECK(frame.header.type == MsgHeartbeatAck);

        auto result = decode_heartbeat_ack(frame);
        auto* msg = std::get_if<HeartbeatAckMessage>(&result);
        RMT_CHECK_MSG(msg != nullptr, "decode_heartbeat_ack succeeded");
        if (msg) {
            RMT_CHECK(msg->sequence == 42);
            RMT_CHECK(msg->received_unix_ms == 1783872000012LL);
        }
    }

    // --- Negative received_unix_ms -> error ---
    {
        std::string payload = "{\"sequence\":0,\"received_unix_ms\":-1}";
        auto frame = make_frame(MsgHeartbeatAck, 0, payload);
        auto result = decode_heartbeat_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "negative received_unix_ms rejected");
    }

    // --- Unknown field -> error ---
    {
        std::string payload = "{\"sequence\":0,\"received_unix_ms\":0,"
                              "\"secret\":\"x\"}";
        auto frame = make_frame(MsgHeartbeatAck, 0, payload);
        auto result = decode_heartbeat_ack(frame);
        auto* err = std::get_if<std::string>(&result);
        RMT_CHECK_MSG(err != nullptr, "unknown field rejected in heartbeat_ack");
    }
}

// --- Cross-type round-trip: encode/decode for all 4 types in sequence ---
void run_cross_roundtrip() {
    // Encode all 4 types, decode each, verify all match.
    // This confirms the decoders correctly distinguish types.

    HelloMessage h;
    h.device_id = "X-001";
    h.agent_version = "2.0.1";
    h.platform = "win32";
    h.protocol_version = 1;
    h.instance_nonce = "AAAA";

    HelloAckMessage ha;
    ha.accepted = true;
    ha.server_version = "0.2.0";
    ha.heartbeat_interval_ms = 5000;
    ha.heartbeat_timeout_ms = 15000;
    ha.max_sessions = 64;

    HeartbeatMessage hb;
    hb.sequence = 100;
    hb.sent_unix_ms = 999;
    hb.active_sessions = 5;

    HeartbeatAckMessage hba;
    hba.sequence = 100;
    hba.received_unix_ms = 1000;

    auto f_hello = encode_hello(h);
    auto f_hello_ack = encode_hello_ack(ha);
    auto f_hb = encode_heartbeat(hb);
    auto f_hba = encode_heartbeat_ack(hba);

    auto r_h = decode_hello(f_hello);
    RMT_CHECK_MSG(std::get_if<HelloMessage>(&r_h) != nullptr, "cross: decode_hello ok");
    auto r_ha = decode_hello_ack(f_hello_ack);
    RMT_CHECK_MSG(std::get_if<HelloAckMessage>(&r_ha) != nullptr, "cross: decode_hello_ack ok");
    auto r_hb = decode_heartbeat(f_hb);
    RMT_CHECK_MSG(std::get_if<HeartbeatMessage>(&r_hb) != nullptr, "cross: decode_heartbeat ok");
    auto r_hba = decode_heartbeat_ack(f_hba);
    RMT_CHECK_MSG(std::get_if<HeartbeatAckMessage>(&r_hba) != nullptr,
                  "cross: decode_heartbeat_ack ok");

    auto* pm_h = std::get_if<HelloMessage>(&r_h);
    auto* pm_ha = std::get_if<HelloAckMessage>(&r_ha);
    auto* pm_hb = std::get_if<HeartbeatMessage>(&r_hb);
    auto* pm_hba = std::get_if<HeartbeatAckMessage>(&r_hba);

    RMT_CHECK(pm_h && pm_h->device_id == "X-001" && pm_h->protocol_version == 1);
    RMT_CHECK(pm_ha && pm_ha->accepted && pm_ha->heartbeat_interval_ms == 5000);
    RMT_CHECK(pm_hb && pm_hb->sequence == 100 && pm_hb->active_sessions == 5);
    RMT_CHECK(pm_hba && pm_hba->received_unix_ms == 1000);

    // Also verify type guards: each decoder rejects wrong type
    {
        auto r1 = decode_hello(f_hello_ack);
        RMT_CHECK(std::get_if<std::string>(&r1) != nullptr);
    }
    {
        auto r2 = decode_hello_ack(f_hb);
        RMT_CHECK(std::get_if<std::string>(&r2) != nullptr);
    }
    {
        auto r3 = decode_heartbeat(f_hba);
        RMT_CHECK(std::get_if<std::string>(&r3) != nullptr);
    }
    {
        auto r4 = decode_heartbeat_ack(f_hello);
        RMT_CHECK(std::get_if<std::string>(&r4) != nullptr);
    }
}

}  // namespace

int main() {
    run_hello_tests();
    run_hello_ack_tests();
    run_heartbeat_tests();
    run_heartbeat_ack_tests();
    run_cross_roundtrip();

    auto& c = rmt::test::ctx();
    std::printf("messages_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
