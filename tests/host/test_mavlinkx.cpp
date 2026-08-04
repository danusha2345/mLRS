#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define FASTMAVLINK_MESSAGE_CRCS {{0, 0, 0, 0, 0, 0}}
#include "mLRS/modules/fastmavlink/c_library/lib/fastmavlink.h"
#include "mLRS/Common/thirdparty/mavlinkx.h"

namespace {

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}

std::vector<uint8_t> pack_bits(std::string bits)
{
    while (bits.size() % 8) bits.push_back('1');

    std::vector<uint8_t> bytes(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); i++) {
        if (bits[i] == '1') bytes[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    }
    return bytes;
}

void test_maximum_payload_round_trip()
{
    std::array<uint8_t, FASTMAVLINK_PAYLOAD_LEN_MAX> payload;
    payload.fill(0xFF);

    std::array<uint8_t, 300> buffer = {};
    uint16_t compressed_len = 0;
    expect(_fmavX_payload_compress(buffer.data(), &compressed_len, payload.data(), payload.size()),
           "maximum payload must be compressible");
    expect(compressed_len < payload.size(), "maximum payload must use a shorter wire representation");

    uint16_t decoded_len = 0;
    expect(_fmavX_payload_decompress(buffer.data(), &decoded_len, compressed_len, payload.size()),
           "maximum payload must decompress");
    expect(decoded_len == payload.size(), "maximum payload length changed");
    expect(std::equal(payload.begin(), payload.end(), buffer.begin()), "maximum payload bytes changed");
}

void test_repeated_rle255_is_bounded()
{
    const std::vector<uint8_t> compressed = pack_bits(
        "00111" "11111111"
        "00111" "11111111");
    std::array<uint8_t, 300> buffer;
    buffer.fill(0xA5);
    std::copy(compressed.begin(), compressed.end(), buffer.begin());

    uint16_t decoded_len = 123;
    expect(!_fmavX_payload_decompress(buffer.data(), &decoded_len, compressed.size(),
                                      FASTMAVLINK_PAYLOAD_LEN_MAX),
           "two RLE255 tokens must exceed the MAVLink payload capacity");
    expect(decoded_len == 0, "failed decompression must not expose a partial payload");
    expect(buffer[FASTMAVLINK_PAYLOAD_LEN_MAX] == 0xA5, "decoder wrote past the payload capacity");
}

void test_overflow_after_full_payload_is_rejected()
{
    const std::vector<uint8_t> compressed = pack_bits(
        "00111" "11111111" // 255 bytes of 0xFF
        "000");             // one more byte
    std::array<uint8_t, 300> buffer;
    buffer.fill(0xA5);
    std::copy(compressed.begin(), compressed.end(), buffer.begin());

    uint16_t decoded_len = 123;
    expect(!_fmavX_payload_decompress(buffer.data(), &decoded_len, compressed.size(),
                                      FASTMAVLINK_PAYLOAD_LEN_MAX),
           "a token after a full payload must be rejected");
    expect(decoded_len == 0, "overflow must clear the reported output length");
    expect(buffer[FASTMAVLINK_PAYLOAD_LEN_MAX] == 0xA5, "overflow changed the first byte past capacity");
}

void test_truncated_token_is_rejected()
{
    const std::vector<uint8_t> compressed = pack_bits("00110"); // RLE0 without its 8-bit count
    std::array<uint8_t, 32> buffer;
    buffer.fill(0xA5);
    std::copy(compressed.begin(), compressed.end(), buffer.begin());

    uint16_t decoded_len = 123;
    expect(!_fmavX_payload_decompress(buffer.data(), &decoded_len, compressed.size(), 16),
           "truncated RLE token must be rejected");
    expect(decoded_len == 0, "truncated token must clear the reported output length");
    expect(buffer[16] == 0xA5, "truncated token wrote past capacity");
}

void test_invalid_code_is_rejected()
{
    const std::vector<uint8_t> compressed = pack_bits("01" "1111110"); // 65..190 code 126 is invalid
    std::array<uint8_t, 32> buffer;
    buffer.fill(0xA5);
    std::copy(compressed.begin(), compressed.end(), buffer.begin());

    uint16_t decoded_len = 123;
    expect(!_fmavX_payload_decompress(buffer.data(), &decoded_len, compressed.size(), 16),
           "invalid literal code must be rejected");
    expect(decoded_len == 0, "invalid code must clear the reported output length");
    expect(buffer[16] == 0xA5, "invalid code wrote past capacity");
}

void test_zero_length_rle_is_rejected()
{
    const std::vector<uint8_t> compressed = pack_bits("00110" "00000000");
    std::array<uint8_t, 32> buffer;
    buffer.fill(0xA5);
    std::copy(compressed.begin(), compressed.end(), buffer.begin());

    uint16_t decoded_len = 123;
    expect(!_fmavX_payload_decompress(buffer.data(), &decoded_len, compressed.size(), 16),
           "zero-length RLE token must be rejected");
    expect(decoded_len == 0, "zero-length RLE token must clear the reported output length");
}

void test_frame_round_trip_stays_wire_compatible()
{
    fmav_message_t message = {};
    message.magic = FASTMAVLINK_MAGIC_V2;
    message.len = FASTMAVLINK_PAYLOAD_LEN_MAX;
    message.seq = 42;
    message.sysid = 7;
    message.compid = 3;
    message.msgid = 200;
    message.checksum = 0xBEEF;
    std::fill_n(message.payload, message.len, 0xFF);

    std::array<uint8_t, 300> expected = {};
    const uint16_t expected_len = fmav_msg_to_frame_buf(expected.data(), &message);

    fmavX_init();
    fmavX_config_compression(1);
    std::array<uint8_t, 300> encoded = {};
    const uint16_t encoded_len = fmavX_msg_to_frame_bufX(encoded.data(), &message);
    expect(encoded[2] & MAVLINKX_FLAGS_IS_COMPRESSED, "test frame was not compressed");

    fmav_status_t status = {};
    fmav_parse_reset(&status);
    fmav_result_t result = {};
    std::array<uint8_t, 300> decoded = {};
    for (uint16_t i = 0; i < encoded_len; i++) {
        fmavX_parseX_to_frame_buf(&result, decoded.data(), &status, encoded[i]);
    }

    expect(result.res == FASTMAVLINK_PARSE_RESULT_OK, "valid compressed frame did not parse");
    expect(result.frame_len == expected_len, "valid compressed frame length changed");
    expect(std::equal(expected.begin(), expected.begin() + expected_len, decoded.begin()),
           "valid compressed frame is not wire-compatible after decoding");
}

void test_parser_resets_after_malformed_payload()
{
    const std::vector<uint8_t> compressed = pack_bits(
        "00111" "11111111"
        "00111" "11111111");
    std::vector<uint8_t> frame = {
        MAVLINKX_MAGIC_1,
        MAVLINKX_MAGIC_2,
        MAVLINKX_FLAGS_IS_COMPRESSED,
        static_cast<uint8_t>(compressed.size()),
        1, 2, 3, 4,
    };
    frame.push_back(fmavX_crc8_calculate(frame.data(), frame.size()));
    frame.insert(frame.end(), compressed.begin(), compressed.end());

    fmavX_init();
    fmav_status_t status = {};
    fmav_parse_reset(&status);
    fmav_result_t result = {};
    std::array<uint8_t, 300> decoded;
    decoded.fill(0xA5);
    for (uint8_t byte : frame) {
        fmavX_parseX_to_frame_buf(&result, decoded.data(), &status, byte);
    }

    expect(result.res == FASTMAVLINK_PARSE_RESULT_NONE, "malformed payload produced a parser result");
    expect(status.rx_state == FASTMAVLINK_PARSE_STATE_IDLE, "parser did not reset after decompression failure");
    expect(decoded[FASTMAVLINK_FRAME_LEN_MAX] == 0xA5, "parser wrote past the regular frame capacity");
}

} // namespace

int main()
{
    test_maximum_payload_round_trip();
    test_repeated_rle255_is_bounded();
    test_overflow_after_full_payload_is_rejected();
    test_truncated_token_is_rejected();
    test_invalid_code_is_rejected();
    test_zero_length_rle_is_rejected();
    test_frame_round_trip_stays_wire_compatible();
    test_parser_resets_after_malformed_payload();
    std::puts("mavlinkx host tests: OK");
    return 0;
}
