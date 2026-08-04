#pragma once

#include <stdint.h>


template <typename TUdp, typename TSerial>
int udp_read_datagram_to_serial(TUdp& udp, TSerial& serial, uint8_t* buf, int buf_size, int packet_size)
{
    if (buf_size <= 0 || packet_size <= 0) return 0;

    int total_len = 0;
    while (total_len < packet_size) {
        int chunk_size = packet_size - total_len;
        if (chunk_size > buf_size) chunk_size = buf_size;

        int len = udp.read(buf, chunk_size);
        if (len <= 0) break;

        serial.write(buf, len);
        total_len += len;
    }
    return total_len;
}
