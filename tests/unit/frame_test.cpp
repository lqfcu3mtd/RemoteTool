// Frame codec tests (PROTOCOL_SPEC.md section 4 + section 18 test vectors).
// Pure C++17, no sockets, no Windows APIs -> compiles under MinGW or MSVC.
#include <cstdint>
#include <vector>

#include "rmt/protocol/frame.h"
#include "rmt/test.h"

using namespace rmt::protocol;
using rmt::ErrorCode;

namespace {

// Build raw 16-byte header (no payload) for malformed-input tests.
std::vector<std::uint8_t> raw_header(MessageType type, std::uint16_t flags,
                                     SessionId session, std::uint32_t plen) {
    std::vector<std::uint8_t> h(16);
    const std::uint32_t magic = kMagic;
    h[0] = static_cast<std::uint8_t>((magic >> 24) & 0xFF);
    h[1] = static_cast<std::uint8_t>((magic >> 16) & 0xFF);
    h[2] = static_cast<std::uint8_t>((magic >> 8) & 0xFF);
    h[3] = static_cast<std::uint8_t>(magic & 0xFF);
    h[4] = kVersion;
    h[5] = type;
    h[6] = static_cast<std::uint8_t>((flags >> 8) & 0xFF);
    h[7] = static_cast<std::uint8_t>(flags & 0xFF);
    h[8] = static_cast<std::uint8_t>((session >> 24) & 0xFF);
    h[9] = static_cast<std::uint8_t>((session >> 16) & 0xFF);
    h[10] = static_cast<std::uint8_t>((session >> 8) & 0xFF);
    h[11] = static_cast<std::uint8_t>(session & 0xFF);
    h[12] = static_cast<std::uint8_t>((plen >> 24) & 0xFF);
    h[13] = static_cast<std::uint8_t>((plen >> 16) & 0xFF);
    h[14] = static_cast<std::uint8_t>((plen >> 8) & 0xFF);
    h[15] = static_cast<std::uint8_t>(plen & 0xFF);
    return h;
}

bool eq(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    return a == b;
}

void run_frame_tests() {
    // --- Official test vector 1: HEARTBEAT, empty payload ---
    {
        const std::vector<std::uint8_t> expected = {
            0x52, 0x4D, 0x54, 0x31, 0x01, 0x03, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        Frame f;
        f.header.type = MsgHeartbeat;
        f.header.session_id = 0;
        auto enc = encode_frame(f);
        RMT_CHECK_MSG(eq(enc, expected), "heartbeat vector mismatch");
        FrameDecoder dec;
        auto r = dec.consume(enc.data(), enc.size());
        RMT_CHECK(r.status == DecodeStatus::Ok);
        RMT_CHECK(r.frames.size() == 1);
        RMT_CHECK(r.frames[0].header.type == MsgHeartbeat);
        RMT_CHECK(r.frames[0].header.session_id == 0);
        RMT_CHECK(r.frames[0].payload.empty());
        RMT_CHECK(dec.buffered() == 0);
    }

    // --- Official test vector 2: SESSION_DATA, session=1, payload "abc" ---
    {
        const std::vector<std::uint8_t> expected = {
            0x52, 0x4D, 0x54, 0x31, 0x01, 0x13, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03,
            0x61, 0x62, 0x63};
        Frame f;
        f.header.type = MsgSessionData;
        f.header.session_id = 1;
        f.payload = {0x61, 0x62, 0x63};
        auto enc = encode_frame(f);
        RMT_CHECK_MSG(eq(enc, expected), "session_data vector mismatch");
        FrameDecoder dec;
        auto r = dec.consume(enc.data(), enc.size());
        RMT_CHECK(r.status == DecodeStatus::Ok);
        RMT_CHECK(r.frames.size() == 1);
        RMT_CHECK(r.frames[0].header.type == MsgSessionData);
        RMT_CHECK(r.frames[0].header.session_id == 1);
        RMT_CHECK(eq(r.frames[0].payload, std::vector<std::uint8_t>{0x61, 0x62, 0x63}));
    }

    // --- Byte-at-a-time feeding of a full frame ---
    {
        Frame f;
        f.header.type = MsgHeartbeat;
        auto enc = encode_frame(f);
        FrameDecoder dec;
        DecodeResult last;
        for (std::size_t i = 0; i < enc.size(); ++i) {
            last = dec.consume(&enc[i], 1);
        }
        RMT_CHECK(last.status == DecodeStatus::Ok);
        RMT_CHECK(last.frames.size() == 1);
        RMT_CHECK(dec.buffered() == 0);
    }

    // --- Multiple frames in one buffer ---
    {
        Frame a;
        a.header.type = MsgHeartbeat;
        Frame b;
        b.header.type = MsgHello;
        auto e1 = encode_frame(a);
        auto e2 = encode_frame(b);
        std::vector<std::uint8_t> both(e1.begin(), e1.end());
        both.insert(both.end(), e2.begin(), e2.end());
        FrameDecoder dec;
        auto r = dec.consume(both.data(), both.size());
        RMT_CHECK(r.status == DecodeStatus::Ok);
        RMT_CHECK(r.frames.size() == 2);
        RMT_CHECK(r.frames[0].header.type == MsgHeartbeat);
        RMT_CHECK(r.frames[1].header.type == MsgHello);
    }

    // --- Partial header -> NeedMore ---
    {
        Frame f;
        f.header.type = MsgHeartbeat;
        auto enc = encode_frame(f);
        FrameDecoder dec;
        auto r = dec.consume(enc.data(), 8);  // only half the header
        RMT_CHECK(r.status == DecodeStatus::NeedMore);
        RMT_CHECK(r.frames.empty());
        RMT_CHECK(dec.buffered() == 8);
    }

    // --- Payload truncated -> NeedMore until complete ---
    {
        Frame f;
        f.header.type = MsgSessionData;
        f.header.session_id = 1;
        f.payload = {0x61, 0x62, 0x63};
        auto enc = encode_frame(f);  // 19 bytes
        FrameDecoder dec;
        auto r = dec.consume(enc.data(), 17);  // missing 2 payload bytes
        RMT_CHECK(r.status == DecodeStatus::NeedMore);
        RMT_CHECK(dec.buffered() == 17);
        auto r2 = dec.consume(enc.data() + 17, 2);
        RMT_CHECK(r2.status == DecodeStatus::Ok);
        RMT_CHECK(r2.frames.size() == 1);
    }

    // --- Bad magic -> Disconnect ---
    {
        auto h = raw_header(MsgHeartbeat, 0, 0, 0);
        h[0] = 0xFF;
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::Disconnect);
        RMT_CHECK(r.error == ErrorCode::InvalidFrame);
    }

    // --- Bad version -> Disconnect ---
    {
        auto h = raw_header(MsgHeartbeat, 0, 0, 0);
        h[4] = 2;
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::Disconnect);
        RMT_CHECK(r.error == ErrorCode::UnsupportedVersion);
    }

    // --- Non-zero flags -> ProtocolError ---
    {
        auto h = raw_header(MsgHeartbeat, 0x0001, 0, 0);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::InvalidFrame);
    }

    // --- Payload length > 1 MiB -> FrameTooLarge ---
    {
        auto h = raw_header(MsgHeartbeat, 0, 0, 1'048'577u);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::FrameTooLarge);
    }

    // --- SESSION_DATA zero length -> InvalidPayload ---
    {
        auto h = raw_header(MsgSessionData, 0, 1, 0);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::InvalidPayload);
    }

    // --- SESSION_DATA > 16 KiB -> FrameTooLarge ---
    {
        auto h = raw_header(MsgSessionData, 0, 1, 16'385u);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::FrameTooLarge);
    }

    // --- Control message with non-zero session -> InvalidPayload ---
    {
        auto h = raw_header(MsgHeartbeat, 0, 5, 0);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::InvalidPayload);
    }

    // --- Session message with zero session -> InvalidPayload ---
    {
        auto h = raw_header(MsgOpenSession, 0, 0, 0);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::InvalidPayload);
    }

    // --- Unknown type -> UnexpectedMessage ---
    {
        auto h = raw_header(0x7F, 0, 0, 0);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::ProtocolError);
        RMT_CHECK(r.error == ErrorCode::UnexpectedMessage);
    }

    // --- PROTOCOL_ERROR with non-zero session is allowed ---
    {
        auto h = raw_header(MsgProtocolError, 0, 7, 0);
        FrameDecoder dec;
        auto r = dec.consume(h.data(), h.size());
        RMT_CHECK(r.status == DecodeStatus::Ok);
        RMT_CHECK(r.frames.size() == 1);
        RMT_CHECK(r.frames[0].header.type == MsgProtocolError);
        RMT_CHECK(r.frames[0].header.session_id == 7);
    }
}

}  // namespace

int main() {
    run_frame_tests();
    auto& c = rmt::test::ctx();
    std::printf("frame_test: %d passed, %d failed\n", c.passed, c.failed);
    return c.ok() ? 0 : 1;
}
