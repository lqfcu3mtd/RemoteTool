#pragma once
// RMT/1 protocol message codec (PROTOCOL_SPEC.md section 6.1-6.6).
// Each message type has a struct, an encode function (struct -> Frame),
// and a decode function (Frame -> variant<Message, error string>).
//
// Decode is strict: type mismatch, malformed JSON, missing fields, type errors,
// out-of-range values, and unknown fields all produce an error string.
// Encode does not validate; validation is the caller's responsibility before
// encoding, and the decoder's responsibility after receiving.

#include <string>
#include <variant>

#include "rmt/common/error_code.h"
#include "rmt/protocol/frame.h"

namespace rmt::protocol {

// --- HELLO (Agent -> RemoteTool) ---

struct HelloMessage {
    std::string device_id;         // 1-64 chars, ASCII alphanum + - _ .
    std::string agent_version;     // 1-32 chars
    std::string platform;          // 1-32 chars
    int protocol_version = 0;      // must be 1
    std::string instance_nonce;    // empty or valid base64url without padding
};

using HelloDecodeResult = std::variant<HelloMessage, std::string>;

Frame encode_hello(const HelloMessage& msg);
HelloDecodeResult decode_hello(const Frame& frame);

// --- HELLO_ACK (RemoteTool -> Agent) ---

struct HelloAckMessage {
    bool accepted = false;
    std::string server_version;        // 1-32 chars
    int heartbeat_interval_ms = 0;     // 1000-60000
    int heartbeat_timeout_ms = 0;      // >= 2*interval_ms, max 180000
    int max_sessions = 0;              // >= 0
    std::string error_code;            // only meaningful when accepted=false, defaults to ""
    std::string message;               // only meaningful when accepted=false, defaults to ""
};

using HelloAckDecodeResult = std::variant<HelloAckMessage, std::string>;

Frame encode_hello_ack(const HelloAckMessage& msg);
HelloAckDecodeResult decode_hello_ack(const Frame& frame);

// --- HEARTBEAT (Agent -> RemoteTool) ---

struct HeartbeatMessage {
    long long sequence = 0;          // >= 0
    long long sent_unix_ms = 0;     // >= 0
    long long active_sessions = 0;  // >= 0
};

using HeartbeatDecodeResult = std::variant<HeartbeatMessage, std::string>;

Frame encode_heartbeat(const HeartbeatMessage& msg);
HeartbeatDecodeResult decode_heartbeat(const Frame& frame);

// --- HEARTBEAT_ACK (RemoteTool -> Agent) ---

struct HeartbeatAckMessage {
    long long sequence = 0;          // >= 0
    long long received_unix_ms = 0;  // >= 0
};

using HeartbeatAckDecodeResult = std::variant<HeartbeatAckMessage, std::string>;

Frame encode_heartbeat_ack(const HeartbeatAckMessage& msg);
HeartbeatAckDecodeResult decode_heartbeat_ack(const Frame& frame);

}  // namespace rmt::protocol
