#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "esp/mlrs-wireless-bridge/udp-drain.h"

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

class FakeUdp {
  public:
    explicit FakeUdp(std::vector<std::vector<uint8_t>> packets, int max_read = 0)
        : packets_(std::move(packets)), max_read_(max_read)
    {}

    int parsePacket()
    {
        if (active_) return 0; // Matches ESP32: unread rx_buffer wedges parsePacket().
        if (next_packet_ >= packets_.size()) return 0;

        active_ = true;
        offset_ = 0;
        return static_cast<int>(packets_[next_packet_].size());
    }

    int read(uint8_t* buf, int len)
    {
        if (!active_) return 0;
        if (max_read_ > 0 && len > max_read_) len = max_read_;

        const std::vector<uint8_t>& packet = packets_[next_packet_];
        const int remaining = static_cast<int>(packet.size() - offset_);
        if (len > remaining) len = remaining;
        std::copy_n(packet.begin() + offset_, len, buf);
        offset_ += static_cast<size_t>(len);
        read_sizes.push_back(len);

        if (offset_ == packet.size()) {
            active_ = false;
            next_packet_++;
        }
        return len;
    }

    std::vector<int> read_sizes;

  private:
    std::vector<std::vector<uint8_t>> packets_;
    size_t next_packet_ = 0;
    size_t offset_ = 0;
    int max_read_;
    bool active_ = false;
};

class FakeSerial {
  public:
    size_t write(const uint8_t* buf, size_t len)
    {
        bytes.insert(bytes.end(), buf, buf + len);
        return len;
    }

    std::vector<uint8_t> bytes;
};

std::vector<uint8_t> make_packet(int size, uint8_t seed)
{
    std::vector<uint8_t> packet(size);
    for (int i = 0; i < size; i++) packet[i] = seed + i;
    return packet;
}

void test_datagram_and_following_heartbeat(int datagram_size)
{
    const std::vector<uint8_t> datagram = make_packet(datagram_size, 0x20);
    const std::vector<uint8_t> heartbeat = make_packet(17, 0xA0);
    FakeUdp udp({datagram, heartbeat});
    FakeSerial serial;
    uint8_t buf[256];

    int packet_size = udp.parsePacket();
    expect(packet_size == datagram_size, "first datagram size changed");
    expect(udp_read_datagram_to_serial(udp, serial, buf, sizeof(buf), packet_size) == datagram_size,
           "first datagram was not fully drained");

    packet_size = udp.parsePacket();
    expect(packet_size == static_cast<int>(heartbeat.size()), "next heartbeat remained wedged");
    expect(udp_read_datagram_to_serial(udp, serial, buf, sizeof(buf), packet_size) == packet_size,
           "next heartbeat was not drained");

    std::vector<uint8_t> expected = datagram;
    expected.insert(expected.end(), heartbeat.begin(), heartbeat.end());
    expect(serial.bytes == expected, "serial output differs from UDP datagrams");
}

void test_short_reads_are_drained()
{
    const std::vector<uint8_t> datagram = make_packet(280, 0x40);
    FakeUdp udp({datagram}, 37);
    FakeSerial serial;
    uint8_t buf[256];

    const int packet_size = udp.parsePacket();
    expect(udp_read_datagram_to_serial(udp, serial, buf, sizeof(buf), packet_size) == packet_size,
           "short UDP reads left an unread tail");
    expect(serial.bytes == datagram, "short UDP reads changed the datagram");
    expect(udp.read_sizes.size() > 2, "short-read path was not exercised");
}

} // namespace

int main()
{
    test_datagram_and_following_heartbeat(256);
    test_datagram_and_following_heartbeat(257);
    test_datagram_and_following_heartbeat(280);
    test_datagram_and_following_heartbeat(1460);
    test_short_reads_are_drained();
    std::puts("udp drain host tests: OK");
    return 0;
}
