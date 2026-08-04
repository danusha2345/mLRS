#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "mLRS/Common/libs/fifo.h"


[[noreturn]] static void fail(const char* message)
{
    std::cerr << "FIFO host test failed: " << message << '\n';
    std::exit(1);
}


static void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}


static void test_putbuf_is_all_or_nothing_at_every_fill_level()
{
    constexpr uint16_t fifo_size = 8;
    constexpr uint16_t capacity = fifo_size - 1;
    const uint8_t payload[10] = { 100, 101, 102, 103, 104, 105, 106, 107, 108, 109 };

    for (uint16_t fill = 0; fill <= capacity; fill++) {
        for (uint16_t len = 0; len <= 10; len++) {
            tFifo<uint8_t, fifo_size> fifo;
            for (uint16_t i = 0; i < fill; i++) {
                expect(fifo.Put((uint8_t)(10 + i)), "could not prepare fill level");
            }

            const bool should_fit = (fill + len <= capacity);
            const bool accepted = fifo.PutBuf(payload, len);
            expect(accepted == should_fit, "PutBuf returned the wrong result");
            expect(fifo.Available() == fill + (should_fit ? len : 0),
                   "rejected buffer changed FIFO length");
            expect(fifo.OverflowCount() == (should_fit ? 0U : 1U),
                   "overflow counter did not match buffer result");

            for (uint16_t i = 0; i < fill; i++) {
                expect(fifo.Get() == (uint8_t)(10 + i),
                       "rejected buffer corrupted existing data");
            }
            if (should_fit) {
                for (uint16_t i = 0; i < len; i++) {
                    expect(fifo.Get() == payload[i], "accepted buffer data is wrong");
                }
            }
            expect(fifo.Available() == 0, "FIFO did not drain completely");
        }
    }
}


static void test_wraparound_and_overflow_accounting()
{
    tFifo<uint8_t, 8> fifo;
    const uint8_t first[] = { 1, 2, 3, 4, 5 };
    const uint8_t second[] = { 6, 7, 8, 9 };

    expect(fifo.PutBuf(first, sizeof(first)), "initial buffer was rejected");
    expect(fifo.Get() == 1 && fifo.Get() == 2 && fifo.Get() == 3,
           "initial data order is wrong");
    expect(fifo.PutBuf(second, sizeof(second)), "wrapped buffer was rejected");

    const uint8_t expected[] = { 4, 5, 6, 7, 8, 9 };
    for (uint8_t value : expected) expect(fifo.Get() == value, "wraparound changed order");

    for (unsigned i = 0; i < 7; i++) expect(fifo.Put((uint8_t)i), "fill failed");
    expect(!fifo.Put(42), "single-byte overflow was accepted");
    expect(fifo.OverflowCount() == 1, "single-byte overflow was not counted");

    fifo.Init();
    expect(fifo.Available() == 0, "Init did not clear FIFO");
    expect(fifo.OverflowCount() == 0, "Init did not clear overflow counter");
}


int main()
{
    test_putbuf_is_all_or_nothing_at_every_fill_level();
    test_wraparound_and_overflow_accounting();
    std::cout << "FIFO host tests passed\n";
    return 0;
}
