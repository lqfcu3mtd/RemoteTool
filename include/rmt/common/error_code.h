#pragma once
#include <string_view>

namespace rmt {

// Stable protocol/connection error codes (PROTOCOL_SPEC.md section 7).
// These are what we send on the wire; platform-specific raw codes stay in logs only.
enum class ErrorCode : int {
    Ok = 0,

    // Connection-level
    UnsupportedVersion,
    InvalidFrame,
    FrameTooLarge,
    InvalidPayload,
    UnexpectedMessage,
    DeviceDisabled,
    DuplicateDeviceConnection,
    HeartbeatTimeout,
    InternalError,

    // Session-level
    SessionLimitReached,
    DuplicateSessionId,
    MappingNotFound,
    TargetNotAllowed,
    TargetConnectTimeout,
    TargetConnectionRefused,
    TargetNetworkUnreachable,
    TargetIoError,
    LocalPeerClosed,
    AgentDisconnected,
    BufferLimitReached,
    Normal,
};

constexpr std::string_view to_string(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::Ok: return "OK";
        case ErrorCode::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case ErrorCode::InvalidFrame: return "INVALID_FRAME";
        case ErrorCode::FrameTooLarge: return "FRAME_TOO_LARGE";
        case ErrorCode::InvalidPayload: return "INVALID_PAYLOAD";
        case ErrorCode::UnexpectedMessage: return "UNEXPECTED_MESSAGE";
        case ErrorCode::DeviceDisabled: return "DEVICE_DISABLED";
        case ErrorCode::DuplicateDeviceConnection: return "DUPLICATE_DEVICE_CONNECTION";
        case ErrorCode::HeartbeatTimeout: return "HEARTBEAT_TIMEOUT";
        case ErrorCode::InternalError: return "INTERNAL_ERROR";
        case ErrorCode::SessionLimitReached: return "SESSION_LIMIT_REACHED";
        case ErrorCode::DuplicateSessionId: return "DUPLICATE_SESSION_ID";
        case ErrorCode::MappingNotFound: return "MAPPING_NOT_FOUND";
        case ErrorCode::TargetNotAllowed: return "TARGET_NOT_ALLOWED";
        case ErrorCode::TargetConnectTimeout: return "TARGET_CONNECT_TIMEOUT";
        case ErrorCode::TargetConnectionRefused: return "TARGET_CONNECTION_REFUSED";
        case ErrorCode::TargetNetworkUnreachable: return "TARGET_NETWORK_UNREACHABLE";
        case ErrorCode::TargetIoError: return "TARGET_IO_ERROR";
        case ErrorCode::LocalPeerClosed: return "LOCAL_PEER_CLOSED";
        case ErrorCode::AgentDisconnected: return "AGENT_DISCONNECTED";
        case ErrorCode::BufferLimitReached: return "BUFFER_LIMIT_REACHED";
        case ErrorCode::Normal: return "NORMAL";
    }
    return "UNKNOWN";
}

constexpr bool is_connection_level(ErrorCode c) noexcept {
    return c >= ErrorCode::UnsupportedVersion && c <= ErrorCode::InternalError;
}

}  // namespace rmt
