#pragma once

#include <stddef.h>


static constexpr size_t SERIAL_RX_BUFFER_SIZE = 2 * 1024;
static constexpr size_t SERIAL_TX_BUFFER_SIZE = 512;


enum tSerialStartupError {
    SERIAL_STARTUP_OK = 0,
    SERIAL_STARTUP_RX_BUFFER_FAILED = 1,
    SERIAL_STARTUP_TX_BUFFER_FAILED = 2,
    SERIAL_STARTUP_DRIVER_FAILED = 3,
};


static inline tSerialStartupError serial_startup_error(
    size_t rx_buffer_size,
    size_t tx_buffer_size,
    bool tx_buffer_required,
    bool driver_started)
{
    if (rx_buffer_size != SERIAL_RX_BUFFER_SIZE) return SERIAL_STARTUP_RX_BUFFER_FAILED;
    if (tx_buffer_required && tx_buffer_size != SERIAL_TX_BUFFER_SIZE) return SERIAL_STARTUP_TX_BUFFER_FAILED;
    if (!driver_started) return SERIAL_STARTUP_DRIVER_FAILED;
    return SERIAL_STARTUP_OK;
}
