#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "esp/mlrs-wireless-bridge/tcp-backpressure.h"

namespace {

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "TCP backpressure host test failed: %s\n", message);
    std::exit(1);
}

void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}

void test_transfer_limit_is_bounded_by_every_capacity()
{
    expect(tcp_bridge_transfer_limit(1000, 1000, 1000) == TCP_BRIDGE_IO_BUDGET,
           "per-loop budget was not enforced");
    expect(tcp_bridge_transfer_limit(17, 1000, 1000) == 17,
           "source availability was ignored");
    expect(tcp_bridge_transfer_limit(1000, 23, 1000) == 23,
           "destination availability was ignored");
    expect(tcp_bridge_transfer_limit(1000, 1000, 31) == 31,
           "working buffer size was ignored");
    expect(tcp_bridge_transfer_limit(10, 0, 10) == 0,
           "full destination accepted more input");
}

void test_full_queue_applies_backpressure_without_overwrite()
{
    tTcpBridgeQueue<8> queue;
    const uint8_t initial[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    const uint8_t extra[] = { 9 };

    expect(queue.Put(initial, sizeof(initial)), "queue rejected its exact capacity");
    expect(queue.Free() == 0, "full queue reports free space");
    expect(!queue.Put(extra, sizeof(extra)), "full queue overwrote pending bytes");
    expect(queue.Available() == sizeof(initial), "rejected write changed queue length");

    const uint8_t* pending = queue.ReadPtr();
    expect(pending != nullptr, "full queue has no readable data");
    for (size_t i = 0; i < sizeof(initial); i++) {
        expect(pending[i] == initial[i], "backpressure corrupted pending bytes");
    }
}

void test_partial_writes_resume_in_order_across_wrap()
{
    tTcpBridgeQueue<16> queue;
    std::vector<uint8_t> input(1000);
    for (size_t i = 0; i < input.size(); i++) input[i] = static_cast<uint8_t>(i);

    std::vector<uint8_t> output;
    size_t input_pos = 0;
    size_t iteration = 0;
    while (input_pos < input.size() || queue.Available() > 0) {
        const size_t chunk = std::min<size_t>(7, input.size() - input_pos);
        if (chunk <= queue.Free()) {
            expect(queue.Put(input.data() + input_pos, chunk), "bounded producer write failed");
            input_pos += chunk;
        }

        size_t write_len = queue.ContiguousAvailable();
        write_len = std::min(write_len, 1 + (iteration % 5));
        if (write_len > 0) {
            const uint8_t* pending = queue.ReadPtr();
            output.insert(output.end(), pending, pending + write_len);
            queue.Consume(write_len);
        }
        iteration++;
    }

    expect(output == input, "partial writes changed byte order or lost data");
    expect(queue.Available() == 0, "queue did not drain after partial writes");
}

void test_counters_saturate()
{
    uint32_t counter = UINT32_MAX - 5;
    tcp_bridge_saturating_add(counter, 4);
    expect(counter == UINT32_MAX - 1, "counter changed before saturation");
    tcp_bridge_saturating_add(counter, 2);
    expect(counter == UINT32_MAX, "counter wrapped instead of saturating");
    tcp_bridge_saturating_add(counter, 1);
    expect(counter == UINT32_MAX, "saturated counter wrapped");
}

} // namespace

int main()
{
    test_transfer_limit_is_bounded_by_every_capacity();
    test_full_queue_applies_backpressure_without_overwrite();
    test_partial_writes_resume_in_order_across_wrap();
    test_counters_saturate();
    std::puts("TCP backpressure host tests: OK");
    return 0;
}
