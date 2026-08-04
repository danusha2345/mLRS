#include <cstdio>
#include <cstdlib>

#include "esp/mlrs-wireless-bridge/serial-startup.h"


[[noreturn]] static void fail(const char* message)
{
    std::fprintf(stderr, "serial startup host test failed: %s\n", message);
    std::exit(1);
}


static void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}


int main()
{
    expect(serial_startup_error(SERIAL_RX_BUFFER_SIZE, SERIAL_TX_BUFFER_SIZE, true, true) ==
               SERIAL_STARTUP_OK,
           "valid ESP32 startup was rejected");
    expect(serial_startup_error(0, SERIAL_TX_BUFFER_SIZE, true, true) ==
               SERIAL_STARTUP_RX_BUFFER_FAILED,
           "failed RX buffer configuration was accepted");
    expect(serial_startup_error(SERIAL_RX_BUFFER_SIZE, 0, true, true) ==
               SERIAL_STARTUP_TX_BUFFER_FAILED,
           "failed TX buffer configuration was accepted");
    expect(serial_startup_error(SERIAL_RX_BUFFER_SIZE, SERIAL_TX_BUFFER_SIZE, true, false) ==
               SERIAL_STARTUP_DRIVER_FAILED,
           "failed ESP32 driver allocation was accepted");
    expect(serial_startup_error(SERIAL_RX_BUFFER_SIZE, 0, false, true) ==
               SERIAL_STARTUP_OK,
           "valid ESP8266 startup was rejected");
    expect(serial_startup_error(SERIAL_RX_BUFFER_SIZE, 0, false, false) ==
               SERIAL_STARTUP_DRIVER_FAILED,
           "failed ESP8266 driver allocation was accepted");

    expect(SERIAL_STARTUP_RX_BUFFER_FAILED == 1 &&
               SERIAL_STARTUP_TX_BUFFER_FAILED == 2 &&
               SERIAL_STARTUP_DRIVER_FAILED == 3,
           "diagnostic pulse codes changed");

    std::puts("serial startup host tests: OK");
    return 0;
}
