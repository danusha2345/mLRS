#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define FASTMAVLINK_MESSAGE_CRCS {{0, 0, 0, 0, 0, 0}}
#define STATIC_ASSERT(condition, message) static_assert(condition, message);
#include "mLRS/modules/fastmavlink/c_library/lib/fastmavlink.h"
#include "mLRS/Common/thirdparty/mavlinkx.h"
#include "mLRS/Common/protocols/msp_protocol.h"

#define CRSF_CRC8_INIT 0

uint8_t crsf_crc8_calc(uint8_t crc, uint8_t data)
{
    crc ^= data;
    for (uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5)
                           : static_cast<uint8_t>(crc << 1);
    }
    return crc;
}

uint8_t crsf_crc8_update(uint8_t crc, const void* buf, uint16_t len)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(buf);
    while (len--) crc = crsf_crc8_calc(crc, *bytes++);
    return crc;
}

char* u8toBCD_s(uint8_t value)
{
    static char text[4];
    std::snprintf(text, sizeof(text), "%u", value);
    return text;
}

char* u16toBCD_s(uint16_t value)
{
    static char text[6];
    std::snprintf(text, sizeof(text), "%u", value);
    return text;
}

#include "mLRS/Common/thirdparty/mspx.h"
#include "mLRS/Common/libs/parser_budget.h"

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

void test_exact_byte_budget()
{
    tParserByteBudget budget;
    for (uint16_t i = 0; i < SERIAL_PARSER_BYTE_BUDGET; i++) {
        expect(budget.Take(), "budget ended before 64 bytes");
    }
    expect(!budget.Take(), "budget accepted a 65th byte");
}

void test_mavlink_parser_resumes_without_loss()
{
    fmav_message_t message = {};
    message.magic = FASTMAVLINK_MAGIC_V2;
    message.len = FASTMAVLINK_PAYLOAD_LEN_MAX;
    message.seq = 42;
    message.sysid = 7;
    message.compid = 3;
    message.msgid = 200;
    message.checksum = 0xBEEF;
    for (uint16_t i = 0; i < message.len; i++) message.payload[i] = static_cast<uint8_t>(i);

    std::array<uint8_t, FASTMAVLINK_FRAME_LEN_MAX> frame = {};
    const uint16_t frame_len = fmav_msg_to_frame_buf(frame.data(), &message);

    std::vector<uint8_t> input(3 * SERIAL_PARSER_BYTE_BUDGET, 0x55);
    input.insert(input.end(), frame.begin(), frame.begin() + frame_len);
    input.insert(input.end(), frame.begin(), frame.begin() + frame_len);

    fmav_status_t status = {};
    fmav_parse_reset(&status);
    fmav_result_t result = {};
    std::array<uint8_t, FASTMAVLINK_FRAME_LEN_MAX> parsed = {};
    size_t offset = 0;
    unsigned completed = 0;

    while (offset < input.size()) {
        tParserByteBudget budget;
        const size_t before = offset;
        while (offset < input.size() && budget.Take()) {
            fmav_parse_to_frame_buf(&result, parsed.data(), &status, input[offset++]);
            if (result.res == FASTMAVLINK_PARSE_RESULT_OK) {
                completed++;
                expect(result.frame_len == frame_len, "MAVLink frame length changed across yields");
                expect(std::equal(frame.begin(), frame.begin() + frame_len, parsed.begin()),
                       "MAVLink frame bytes changed across yields");
            }
        }
        expect(offset - before <= SERIAL_PARSER_BYTE_BUDGET,
               "MAVLink invocation exceeded its byte budget");
    }

    expect(completed == 2, "MAVLink frames were lost or duplicated across yields");
}

void test_msp_parser_resumes_without_loss()
{
    std::array<uint8_t, MSP_PAYLOAD_LEN_MAX> payload = {};
    for (uint16_t i = 0; i < payload.size(); i++) payload[i] = static_cast<uint8_t>(i);

    std::array<uint8_t, MSP_FRAME_LEN_MAX> frame = {};
    const uint16_t frame_len = msp_generate_v2_frame_buf(
        frame.data(), MSP_TYPE_RESPONSE, MSP_FLAG_NONE, 0x1234, payload.data(), payload.size());

    msp_status_t status = {};
    msp_status_reset(&status);
    msp_message_t parsed = {};
    size_t offset = 0;
    unsigned completed = 0;

    while (offset < frame_len) {
        tParserByteBudget budget;
        const size_t before = offset;
        while (offset < frame_len && budget.Take()) {
            if (msp_parse_to_msg(&parsed, &status, frame[offset++])) completed++;
        }
        expect(offset - before <= SERIAL_PARSER_BYTE_BUDGET,
               "MSP invocation exceeded its byte budget");
    }

    expect(completed == 1, "MSP frame was lost or duplicated across yields");
    expect(parsed.len == payload.size(), "MSP payload length changed across yields");
    expect(std::equal(payload.begin(), payload.end(), parsed.payload),
           "MSP payload bytes changed across yields");
}

} // namespace

int main()
{
    test_exact_byte_budget();
    test_mavlink_parser_resumes_without_loss();
    test_msp_parser_resumes_without_loss();
    std::puts("parser budget host tests: OK");
    return 0;
}
