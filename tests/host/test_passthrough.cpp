#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>

#define INCc(a, period) do { (a)++; if ((a) >= (period)) (a) = 0; } while (0)

#include "mLRS/Common/mavlink/out/ardupilotmega/ardupilotmega.h"
#include "mLRS/Common/protocols/crsf_protocol.h"
#include "mLRS/Common/protocols/passthrough_protocol.h"


int32_t mav_battery_voltage(fmav_battery_status_t* const)
{
    return 0;
}


[[noreturn]] static void fail(const char* message)
{
    std::cerr << "Passthrough host test failed: " << message << '\n';
    std::exit(1);
}


static void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}


static void test_rpm_packing_and_saturation()
{
    tPassThrough passthrough;
    passthrough.Init();

    uint32_t data = 0;
    expect(!passthrough.get_Rpm_0x500A(&data), "RPM was published before input");

    fmav_rpm_t rpm = {};
    rpm.rpm1 = 1234.0f;
    rpm.rpm2 = -5678.0f;
    passthrough.handle_mavlink_msg_rpm(&rpm);
    expect(passthrough.get_Rpm_0x500A(&data), "RPM was not published");
    expect(data == 0xFDC8007BU, "RPM wire packing changed");
    expect(!passthrough.get_Rpm_0x500A(&data), "RPM update was published twice");

    rpm.rpm1 = 400000.0f;
    rpm.rpm2 = -400000.0f;
    passthrough.handle_mavlink_msg_rpm(&rpm);
    expect(passthrough.get_Rpm_0x500A(&data), "saturated RPM was not published");
    expect(data == 0x80007FFFU, "RPM saturation is wrong");
}


static void test_wind_packing()
{
    tPassThrough passthrough;
    passthrough.Init();

    uint32_t data = 0;
    expect(!passthrough.get_Wind_0x500C(&data), "wind was published before input");

    fmav_wind_t wind = {};
    wind.direction = -30.0f;
    wind.speed = 12.3f;
    passthrough.handle_mavlink_msg_wind(&wind);
    expect(passthrough.get_Wind_0x500C(&data), "wind was not published");
    expect(data == 0x00000CEEU, "wind wire packing changed");
    expect(!passthrough.get_Wind_0x500C(&data), "wind update was published twice");
}


static void test_non_finite_values_fail_closed()
{
    tPassThrough passthrough;
    passthrough.Init();

    fmav_rpm_t rpm = {};
    rpm.rpm1 = std::numeric_limits<float>::quiet_NaN();
    rpm.rpm2 = std::numeric_limits<float>::infinity();
    passthrough.handle_mavlink_msg_rpm(&rpm);

    uint32_t data = 0;
    expect(passthrough.get_Rpm_0x500A(&data), "non-finite RPM was not consumed");
    expect(data == 0x7FFF0000U, "non-finite RPM did not fail closed");

    fmav_wind_t wind = {};
    wind.direction = -std::numeric_limits<float>::infinity();
    wind.speed = std::numeric_limits<float>::quiet_NaN();
    passthrough.handle_mavlink_msg_wind(&wind);
    expect(passthrough.get_Wind_0x500C(&data), "non-finite wind was not consumed");
    expect(data == 0U, "non-finite wind did not fail closed");
}


static void test_rpm_and_wind_multi_frame()
{
    tPassThrough passthrough;
    passthrough.Init();

    fmav_rpm_t rpm = {};
    rpm.rpm1 = 1000.0f;
    rpm.rpm2 = 2000.0f;
    passthrough.handle_mavlink_msg_rpm(&rpm);

    fmav_wind_t wind = {};
    wind.direction = 90.0f;
    wind.speed = 5.0f;
    passthrough.handle_mavlink_msg_wind(&wind);

    uint8_t frame[sizeof(tCrsfPassthroughMulti)] = {};
    uint8_t len = 0;
    expect(passthrough.GetTelemetryFrameMulti(frame, &len), "multi frame was not published");
    expect(len == 14U, "multi frame length is wrong");

    tCrsfPassthroughMulti multi = {};
    std::memcpy(&multi, frame, len);
    expect(multi.sub_type == CRSF_AP_CUSTOM_TELEM_TYPE_MULTI_PACKET_PASSTHROUGH,
           "multi frame subtype is wrong");
    expect(multi.count == 2U, "multi frame packet count is wrong");
    expect(multi.packet[0].packet_type == 0x500AU, "RPM packet id is wrong");
    expect(multi.packet[0].data == 0x00C80064U, "RPM packet data is wrong");
    expect(multi.packet[1].packet_type == 0x500CU, "wind packet id is wrong");
    expect(multi.packet[1].data == 0x0000321EU, "wind packet data is wrong");
}


int main()
{
    test_rpm_packing_and_saturation();
    test_wind_packing();
    test_non_finite_values_fail_closed();
    test_rpm_and_wind_multi_frame();
    std::cout << "Passthrough host tests: OK\n";
    return 0;
}
