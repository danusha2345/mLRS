#pragma once

#include <stddef.h>
#include <stdint.h>


static constexpr size_t TCP_BRIDGE_QUEUE_SIZE = 2 * 1024;
static constexpr size_t TCP_BRIDGE_IO_BUDGET = 256;


inline size_t tcp_bridge_transfer_limit(
    size_t source_available, size_t destination_available, size_t buffer_size)
{
    size_t len = source_available;
    if (len > destination_available) len = destination_available;
    if (len > buffer_size) len = buffer_size;
    if (len > TCP_BRIDGE_IO_BUDGET) len = TCP_BRIDGE_IO_BUDGET;
    return len;
}


inline void tcp_bridge_saturating_add(uint32_t& counter, size_t value)
{
    if (value >= UINT32_MAX || counter > UINT32_MAX - value) {
        counter = UINT32_MAX;
    } else {
        counter += static_cast<uint32_t>(value);
    }
}


template <size_t SIZE>
class tTcpBridgeQueue
{
    static_assert(SIZE > 0, "TCP bridge queue must not be empty");

  public:
    tTcpBridgeQueue() { Reset(); }

    void Reset()
    {
        read_pos_ = 0;
        write_pos_ = 0;
        count_ = 0;
    }

    size_t Available() const { return count_; }
    size_t Free() const { return SIZE - count_; }

    bool Put(const uint8_t* data, size_t len)
    {
        if (len > Free()) return false;

        for (size_t i = 0; i < len; i++) {
            data_[write_pos_] = data[i];
            write_pos_++;
            if (write_pos_ == SIZE) write_pos_ = 0;
        }
        count_ += len;
        return true;
    }

    const uint8_t* ReadPtr() const
    {
        return (count_ > 0) ? &data_[read_pos_] : nullptr;
    }

    size_t ContiguousAvailable() const
    {
        const size_t to_end = SIZE - read_pos_;
        return (count_ < to_end) ? count_ : to_end;
    }

    void Consume(size_t len)
    {
        if (len > count_) len = count_;
        read_pos_ = (read_pos_ + len) % SIZE;
        count_ -= len;
    }

  private:
    uint8_t data_[SIZE];
    size_t read_pos_;
    size_t write_pos_;
    size_t count_;
};
