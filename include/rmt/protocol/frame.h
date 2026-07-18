#pragma once
// RMT/1 frame codec (PROTOCOL_SPEC.md section 4).
// All multi-byte integers are big-endian (network order). The decoder is
// incremental: feed it arbitrary chunks and it produces 0+ complete frames per
// call. It never scans the stream for a magic byte to resync, and it never
// silently tolerates malformed input.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "rmt/common/error_code.h"

namespace rmt::protocol {

using MessageType = std::uint8_t;
using SessionId = std::uint32_t;

constexpr std::uint32_t kMagic = 0x524D5431u;  // ASCII "RMT1"
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::uint32_t kMaxPayloadLength = 1'048'576;     // hard frame cap
constexpr std::uint32_t kMaxSessionDataPayload = 16'384;   // SESSION_DATA cap

// Message type values (PROTOCOL_SPEC.md section 5).
enum MessageTypeEnum : MessageType {
    MsgHello = 0x01,
    MsgHelloAck = 0x02,
    MsgHeartbeat = 0x03,
    MsgHeartbeatAck = 0x04,
    MsgPairProvision = 0x05,
    MsgPairProvisionAck = 0x06,
    MsgOpenSession = 0x10,
    MsgSessionOpened = 0x11,
    MsgSessionOpenFailed = 0x12,
    MsgSessionData = 0x13,
    MsgSessionHalfClose = 0x14,
    MsgCloseSession = 0x15,
    MsgProtocolError = 0x20,
};

struct FrameHeader {
    std::uint8_t version = kVersion;
    MessageType type = 0;
    std::uint16_t flags = 0;
    SessionId session_id = 0;
    std::uint32_t payload_length = 0;
};

struct Frame {
    FrameHeader header;
    std::vector<std::uint8_t> payload;
};

// Encode a complete frame (header + payload) into wire bytes.
std::vector<std::uint8_t> encode_frame(const Frame& frame);

enum class DecodeStatus {
    Ok,          // produced >=0 frames; check result.frames
    NeedMore,    // need more bytes to complete the current frame
    ProtocolError,  // send PROTOCOL_ERROR and tear down the tunnel
    Disconnect,     // hard tear down (bad magic/version), no negotiation
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    ErrorCode error = ErrorCode::Ok;
    std::vector<Frame> frames;  // frames produced by the most recent consume()
};

// Incremental, allocation-bounded frame decoder.
class FrameDecoder {
public:
    // Consume a chunk of bytes. May produce 0, 1, or several frames.
    DecodeResult consume(const std::uint8_t* data, std::size_t len);
    void reset() { buf_.clear(); }
    std::size_t buffered() const noexcept { return buf_.size(); }

private:
    std::vector<std::uint8_t> buf_;
};

}  // namespace rmt::protocol
