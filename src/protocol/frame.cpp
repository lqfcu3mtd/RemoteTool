#include "rmt/protocol/frame.h"

#include <algorithm>
#include <cstring>

namespace rmt::protocol {
namespace {

inline void put_be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}
inline void put_be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

// Returns false on overflow (caller treats as protocol error).
inline bool parse_header(const std::uint8_t* p, FrameHeader& h) {
    std::uint32_t magic = (static_cast<std::uint32_t>(p[0]) << 24) |
                          (static_cast<std::uint32_t>(p[1]) << 16) |
                          (static_cast<std::uint32_t>(p[2]) << 8) |
                          (static_cast<std::uint32_t>(p[3]));
    if (magic != kMagic) return false;
    h.version = p[4];
    h.type = p[5];
    h.flags = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[6]) << 8) |
                                         static_cast<std::uint16_t>(p[7]));
    h.session_id = (static_cast<std::uint32_t>(p[8]) << 24) |
                   (static_cast<std::uint32_t>(p[9]) << 16) |
                   (static_cast<std::uint32_t>(p[10]) << 8) |
                   (static_cast<std::uint32_t>(p[11]));
    h.payload_length = (static_cast<std::uint32_t>(p[12]) << 24) |
                       (static_cast<std::uint32_t>(p[13]) << 16) |
                       (static_cast<std::uint32_t>(p[14]) << 8) |
                       (static_cast<std::uint32_t>(p[15]));
    return true;
}

inline bool is_control_message(MessageType t) {
    switch (t) {
        case MsgHello:
        case MsgHelloAck:
        case MsgHeartbeat:
        case MsgHeartbeatAck:
        case MsgPairProvision:
        case MsgPairProvisionAck:
            return true;
        default:
            return false;
    }
}
inline bool is_session_message(MessageType t) {
    switch (t) {
        case MsgOpenSession:
        case MsgSessionOpened:
        case MsgSessionOpenFailed:
        case MsgSessionData:
        case MsgSessionHalfClose:
        case MsgCloseSession:
            return true;
        default:
            return false;
    }
}

}  // namespace

std::vector<std::uint8_t> encode_frame(const Frame& frame) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + frame.payload.size());
    put_be32(out, kMagic);
    out.push_back(frame.header.version);
    out.push_back(frame.header.type);
    put_be16(out, frame.header.flags);
    put_be32(out, frame.header.session_id);
    put_be32(out, static_cast<std::uint32_t>(frame.payload.size()));
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    return out;
}

DecodeResult FrameDecoder::consume(const std::uint8_t* data, std::size_t len) {
    DecodeResult result;
    if (len > 0) {
        buf_.insert(buf_.end(), data, data + len);
    }

    while (true) {
        if (buf_.size() < kHeaderSize) {
            result.status = result.frames.empty() ? DecodeStatus::NeedMore : DecodeStatus::Ok;
            return result;
        }

        FrameHeader h{};
        if (!parse_header(buf_.data(), h)) {
            // Bad magic: hard disconnect, never negotiate.
            result.status = DecodeStatus::Disconnect;
            result.error = ErrorCode::InvalidFrame;
            return result;
        }
        if (h.version != kVersion) {
            result.status = DecodeStatus::Disconnect;
            result.error = ErrorCode::UnsupportedVersion;
            return result;
        }
        if (h.flags != 0) {
            result.status = DecodeStatus::ProtocolError;
            result.error = ErrorCode::InvalidFrame;
            return result;
        }

        // Payload length bounds check (also guards integer overflow below).
        if (h.payload_length > kMaxPayloadLength) {
            result.status = DecodeStatus::ProtocolError;
            result.error = ErrorCode::FrameTooLarge;
            return result;
        }

        // Unknown type.
        if (!is_control_message(h.type) && !is_session_message(h.type) &&
            h.type != MsgProtocolError) {
            result.status = DecodeStatus::ProtocolError;
            result.error = ErrorCode::UnexpectedMessage;
            return result;
        }

        // Session-id rules.
        if (h.type == MsgProtocolError) {
            // allowed with session 0 or a related id
        } else if (is_control_message(h.type) && h.session_id != 0) {
            result.status = DecodeStatus::ProtocolError;
            result.error = ErrorCode::InvalidPayload;
            return result;
        } else if (is_session_message(h.type) && h.session_id == 0) {
            result.status = DecodeStatus::ProtocolError;
            result.error = ErrorCode::InvalidPayload;
            return result;
        }

        // SESSION_DATA payload constraints.
        if (h.type == MsgSessionData) {
            if (h.payload_length == 0) {
                result.status = DecodeStatus::ProtocolError;
                result.error = ErrorCode::InvalidPayload;
                return result;
            }
            if (h.payload_length > kMaxSessionDataPayload) {
                result.status = DecodeStatus::ProtocolError;
                result.error = ErrorCode::FrameTooLarge;
                return result;
            }
        }

        const std::size_t total = kHeaderSize + static_cast<std::size_t>(h.payload_length);
        if (buf_.size() < total) {
            result.status = result.frames.empty() ? DecodeStatus::NeedMore : DecodeStatus::Ok;
            return result;
        }

        Frame f;
        f.header = h;
        f.payload.assign(buf_.begin() + kHeaderSize, buf_.begin() + total);
        result.frames.push_back(std::move(f));

        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(total));
    }
}

}  // namespace rmt::protocol
