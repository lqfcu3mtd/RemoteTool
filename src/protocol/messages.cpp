#include "rmt/protocol/messages.h"

#include <cstdio>
#include <string>
#include <unordered_set>
#include <variant>

#include "rmt/config/strict_json.h"
#include "rmt/protocol/frame.h"

namespace rmt::protocol {
namespace {

// --- JSON string escaping for manual encoding ---

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

// --- Payload helpers: string<->vector ---

std::string payload_to_string(const std::vector<std::uint8_t>& payload) {
    return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

std::vector<std::uint8_t> string_to_payload(const std::string& s) {
    return {s.begin(), s.end()};
}

// --- Decode helpers ---
// All extract_* functions follow this convention:
//   Returns "" on success (with the value in `out`).
//   Returns an error description string on failure.

std::string check_frame_type(const Frame& frame, MessageType expected) {
    if (frame.header.type != expected) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "frame type mismatch: expected 0x%02x, got 0x%02x",
                      static_cast<unsigned>(expected),
                      static_cast<unsigned>(frame.header.type));
        return buf;
    }
    return {};
}

// Parse payload as JSON. Returns either the parsed JsonValue or an error string.
using ParseResult = std::variant<rmt::config::JsonValue, std::string>;

ParseResult parse_payload_json(const Frame& frame) {
    std::string payload_str = payload_to_string(frame.payload);
    auto parse_result = rmt::config::parse_json(payload_str);
    const auto* err = rmt::config::try_get_json_error(parse_result);
    if (err) {
        return "JSON parse error at line " + std::to_string(err->line)
             + " col " + std::to_string(err->column) + ": " + err->reason;
    }
    auto* val = rmt::config::try_get_json_value(parse_result);
    if (!val) {
        return "JSON parse returned null result";
    }
    return std::move(*val);
}

std::string check_unknown_fields(const rmt::config::JsonValue& obj,
                                 const std::unordered_set<std::string>& allowed) {
    const auto* obj_ptr = obj.try_as_object();
    if (!obj_ptr) {
        return "internal error: expected JSON object";
    }
    for (const auto& kv : *obj_ptr) {
        if (allowed.find(kv.first) == allowed.end()) {
            return "unknown field: \"" + kv.first + "\"";
        }
    }
    return {};
}

std::string extract_string(const rmt::config::JsonValue& obj,
                           const std::string& field,
                           std::string& out) {
    const auto* val = obj.find(field);
    if (!val) {
        return "missing required field: \"" + field + "\"";
    }
    const auto* str = val->try_as_string();
    if (!str) {
        return "field \"" + field + "\" must be a string";
    }
    out = *str;
    return {};
}

std::string extract_int(const rmt::config::JsonValue& obj,
                        const std::string& field,
                        long long& out) {
    const auto* val = obj.find(field);
    if (!val) {
        return "missing required field: \"" + field + "\"";
    }
    auto i = val->as_int();
    if (!i.has_value()) {
        return "field \"" + field + "\" must be an integer";
    }
    out = *i;
    return {};
}

std::string extract_bool(const rmt::config::JsonValue& obj,
                         const std::string& field,
                         bool& out) {
    const auto* val = obj.find(field);
    if (!val) {
        return "missing required field: \"" + field + "\"";
    }
    auto b = val->as_bool();
    if (!b.has_value()) {
        return "field \"" + field + "\" must be a boolean";
    }
    out = *b;
    return {};
}

std::string extract_optional_string(const rmt::config::JsonValue& obj,
                                    const std::string& field,
                                    std::string& out) {
    const auto* val = obj.find(field);
    if (!val || val->is_null()) {
        out.clear();
        return {};
    }
    const auto* str = val->try_as_string();
    out = str ? *str : std::string{};
    return {};
}

// --- Field validators ---

bool is_valid_device_id(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '-' || c == '_' || c == '.') continue;
        return false;
    }
    return true;
}

bool is_valid_version_string(const std::string& s) {
    return !s.empty() && s.size() <= 32;
}

bool is_valid_platform(const std::string& s) {
    return !s.empty() && s.size() <= 32;
}

bool is_valid_base64url(const std::string& s) {
    for (char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) continue;
        if (c >= '0' && c <= '9') continue;
        if (c == '-' || c == '_') continue;
        return false;
    }
    return true;
}

}  // namespace

// ====== Common decode preamble ======
// Extracts JSON object from frame payload. Returns error string on failure.
// On success, `json_obj` contains the parsed JSON and `obj` points to it.
template <typename DecodeResult>
std::string decode_preamble(const Frame& frame, MessageType expected,
                             rmt::config::JsonValue& json_obj,
                             const rmt::config::JsonValue*& obj) {
    std::string type_err = check_frame_type(frame, expected);
    if (!type_err.empty()) return type_err;

    auto parse = parse_payload_json(frame);
    if (auto* err = std::get_if<std::string>(&parse)) {
        return *err;
    }
    json_obj = std::move(std::get<rmt::config::JsonValue>(parse));
    if (!json_obj.is_object()) {
        return "payload must be a JSON object";
    }
    obj = &json_obj;
    return {};
}

// ====== HELLO ======

Frame encode_hello(const HelloMessage& msg) {
    std::string json;
    json += "{\"device_id\":\"";
    json += json_escape(msg.device_id);
    json += "\",\"agent_version\":\"";
    json += json_escape(msg.agent_version);
    json += "\",\"platform\":\"";
    json += json_escape(msg.platform);
    json += "\",\"protocol_version\":";
    json += std::to_string(msg.protocol_version);
    json += ",\"instance_nonce\":\"";
    json += json_escape(msg.instance_nonce);
    json += "\"}";

    Frame frame;
    frame.header.type = MsgHello;
    frame.header.session_id = 0;
    frame.payload = string_to_payload(json);
    return frame;
}

HelloDecodeResult decode_hello(const Frame& frame) {
    rmt::config::JsonValue json_obj;
    const rmt::config::JsonValue* obj = nullptr;

    std::string pre_err = decode_preamble<HelloDecodeResult>(frame, MsgHello, json_obj, obj);
    if (!pre_err.empty()) return pre_err;

    static const std::unordered_set<std::string> kAllowed = {
        "device_id", "agent_version", "platform", "protocol_version", "instance_nonce"
    };
    std::string unknown_err = check_unknown_fields(*obj, kAllowed);
    if (!unknown_err.empty()) return unknown_err;

    std::string err;

    // device_id
    std::string device_id;
    err = extract_string(*obj, "device_id", device_id);
    if (!err.empty()) return err;
    if (!is_valid_device_id(device_id)) {
        return "device_id invalid: must be 1-64 chars, only ASCII alphanum + '-' '_' '.'";
    }

    // agent_version
    std::string agent_version;
    err = extract_string(*obj, "agent_version", agent_version);
    if (!err.empty()) return err;
    if (!is_valid_version_string(agent_version)) {
        return "agent_version invalid: must be 1-32 chars";
    }

    // platform
    std::string platform;
    err = extract_string(*obj, "platform", platform);
    if (!err.empty()) return err;
    if (!is_valid_platform(platform)) {
        return "platform invalid: must be 1-32 chars";
    }

    // protocol_version
    long long proto_val = 0;
    err = extract_int(*obj, "protocol_version", proto_val);
    if (!err.empty()) return err;
    if (proto_val != 1) {
        return "protocol_version must be 1, got " + std::to_string(proto_val);
    }

    // instance_nonce
    std::string instance_nonce;
    err = extract_string(*obj, "instance_nonce", instance_nonce);
    if (!err.empty()) return err;
    if (!instance_nonce.empty() && !is_valid_base64url(instance_nonce)) {
        return "instance_nonce must be valid base64url or empty";
    }

    HelloMessage msg;
    msg.device_id = std::move(device_id);
    msg.agent_version = std::move(agent_version);
    msg.platform = std::move(platform);
    msg.protocol_version = static_cast<int>(proto_val);
    msg.instance_nonce = std::move(instance_nonce);
    return msg;
}

// ====== HELLO_ACK ======

Frame encode_hello_ack(const HelloAckMessage& msg) {
    std::string json;
    json += "{\"accepted\":";
    json += msg.accepted ? "true" : "false";
    json += ",\"server_version\":\"";
    json += json_escape(msg.server_version);
    json += "\",\"heartbeat_interval_ms\":";
    json += std::to_string(msg.heartbeat_interval_ms);
    json += ",\"heartbeat_timeout_ms\":";
    json += std::to_string(msg.heartbeat_timeout_ms);
    json += ",\"max_sessions\":";
    json += std::to_string(msg.max_sessions);
    if (!msg.error_code.empty() || !msg.message.empty()) {
        json += ",\"error_code\":\"";
        json += json_escape(msg.error_code);
        json += "\",\"message\":\"";
        json += json_escape(msg.message);
        json += "\"";
    }
    json += "}";

    Frame frame;
    frame.header.type = MsgHelloAck;
    frame.header.session_id = 0;
    frame.payload = string_to_payload(json);
    return frame;
}

HelloAckDecodeResult decode_hello_ack(const Frame& frame) {
    rmt::config::JsonValue json_obj;
    const rmt::config::JsonValue* obj = nullptr;

    std::string pre_err = decode_preamble<HelloAckDecodeResult>(frame, MsgHelloAck, json_obj, obj);
    if (!pre_err.empty()) return pre_err;

    static const std::unordered_set<std::string> kAllowed = {
        "accepted", "server_version", "heartbeat_interval_ms",
        "heartbeat_timeout_ms", "max_sessions", "error_code", "message"
    };
    std::string unknown_err = check_unknown_fields(*obj, kAllowed);
    if (!unknown_err.empty()) return unknown_err;

    std::string err;

    // accepted
    bool accepted = false;
    err = extract_bool(*obj, "accepted", accepted);
    if (!err.empty()) return err;

    // server_version
    std::string server_version;
    err = extract_string(*obj, "server_version", server_version);
    if (!err.empty()) return err;
    if (!is_valid_version_string(server_version)) {
        return "server_version invalid: must be 1-32 chars";
    }

    // heartbeat_interval_ms
    long long interval_val = 0;
    err = extract_int(*obj, "heartbeat_interval_ms", interval_val);
    if (!err.empty()) return err;
    if (interval_val < 1000 || interval_val > 60000) {
        return "heartbeat_interval_ms out of range [1000, 60000], got "
             + std::to_string(interval_val);
    }

    // heartbeat_timeout_ms
    long long timeout_val = 0;
    err = extract_int(*obj, "heartbeat_timeout_ms", timeout_val);
    if (!err.empty()) return err;
    if (timeout_val > 180000) {
        return "heartbeat_timeout_ms out of range (max 180000), got "
             + std::to_string(timeout_val);
    }
    if (timeout_val < 2 * interval_val) {
        return "heartbeat_timeout_ms must be >= 2 * heartbeat_interval_ms ("
             + std::to_string(2 * interval_val) + "), got " + std::to_string(timeout_val);
    }

    // max_sessions
    long long sessions_val = 0;
    err = extract_int(*obj, "max_sessions", sessions_val);
    if (!err.empty()) return err;
    if (sessions_val < 0) {
        return "max_sessions must be >= 0, got " + std::to_string(sessions_val);
    }

    // error_code and message (optional)
    std::string error_code;
    std::string message;
    extract_optional_string(*obj, "error_code", error_code);
    extract_optional_string(*obj, "message", message);

    HelloAckMessage msg;
    msg.accepted = accepted;
    msg.server_version = std::move(server_version);
    msg.heartbeat_interval_ms = static_cast<int>(interval_val);
    msg.heartbeat_timeout_ms = static_cast<int>(timeout_val);
    msg.max_sessions = static_cast<int>(sessions_val);
    msg.error_code = std::move(error_code);
    msg.message = std::move(message);
    return msg;
}

// ====== HEARTBEAT ======

Frame encode_heartbeat(const HeartbeatMessage& msg) {
    std::string json;
    json += "{\"sequence\":";
    json += std::to_string(msg.sequence);
    json += ",\"sent_unix_ms\":";
    json += std::to_string(msg.sent_unix_ms);
    json += ",\"active_sessions\":";
    json += std::to_string(msg.active_sessions);
    json += "}";

    Frame frame;
    frame.header.type = MsgHeartbeat;
    frame.header.session_id = 0;
    frame.payload = string_to_payload(json);
    return frame;
}

HeartbeatDecodeResult decode_heartbeat(const Frame& frame) {
    rmt::config::JsonValue json_obj;
    const rmt::config::JsonValue* obj = nullptr;

    std::string pre_err = decode_preamble<HeartbeatDecodeResult>(frame, MsgHeartbeat, json_obj, obj);
    if (!pre_err.empty()) return pre_err;

    static const std::unordered_set<std::string> kAllowed = {
        "sequence", "sent_unix_ms", "active_sessions"
    };
    std::string unknown_err = check_unknown_fields(*obj, kAllowed);
    if (!unknown_err.empty()) return unknown_err;

    std::string err;

    long long sequence = 0;
    err = extract_int(*obj, "sequence", sequence);
    if (!err.empty()) return err;
    if (sequence < 0) {
        return "sequence must be >= 0, got " + std::to_string(sequence);
    }

    long long sent_unix_ms = 0;
    err = extract_int(*obj, "sent_unix_ms", sent_unix_ms);
    if (!err.empty()) return err;
    if (sent_unix_ms < 0) {
        return "sent_unix_ms must be >= 0, got " + std::to_string(sent_unix_ms);
    }

    long long active_sessions = 0;
    err = extract_int(*obj, "active_sessions", active_sessions);
    if (!err.empty()) return err;
    if (active_sessions < 0) {
        return "active_sessions must be >= 0, got " + std::to_string(active_sessions);
    }

    HeartbeatMessage msg;
    msg.sequence = sequence;
    msg.sent_unix_ms = sent_unix_ms;
    msg.active_sessions = active_sessions;
    return msg;
}

// ====== HEARTBEAT_ACK ======

Frame encode_heartbeat_ack(const HeartbeatAckMessage& msg) {
    std::string json;
    json += "{\"sequence\":";
    json += std::to_string(msg.sequence);
    json += ",\"received_unix_ms\":";
    json += std::to_string(msg.received_unix_ms);
    json += "}";

    Frame frame;
    frame.header.type = MsgHeartbeatAck;
    frame.header.session_id = 0;
    frame.payload = string_to_payload(json);
    return frame;
}

HeartbeatAckDecodeResult decode_heartbeat_ack(const Frame& frame) {
    rmt::config::JsonValue json_obj;
    const rmt::config::JsonValue* obj = nullptr;

    std::string pre_err = decode_preamble<HeartbeatAckDecodeResult>(frame, MsgHeartbeatAck, json_obj, obj);
    if (!pre_err.empty()) return pre_err;

    static const std::unordered_set<std::string> kAllowed = {
        "sequence", "received_unix_ms"
    };
    std::string unknown_err = check_unknown_fields(*obj, kAllowed);
    if (!unknown_err.empty()) return unknown_err;

    std::string err;

    long long sequence = 0;
    err = extract_int(*obj, "sequence", sequence);
    if (!err.empty()) return err;

    long long received_unix_ms = 0;
    err = extract_int(*obj, "received_unix_ms", received_unix_ms);
    if (!err.empty()) return err;
    if (received_unix_ms < 0) {
        return "received_unix_ms must be >= 0, got " + std::to_string(received_unix_ms);
    }

    HeartbeatAckMessage msg;
    msg.sequence = sequence;
    msg.received_unix_ms = received_unix_ms;
    return msg;
}

}  // namespace rmt::protocol
