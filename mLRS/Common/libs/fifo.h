//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
// OlliW @ www.olliw.eu
//*******************************************************
// FIFO
//********************************************************
#ifndef FIFO_H
#define FIFO_H
#pragma once


#include <inttypes.h>

template <class T, uint16_t FIFO_SIZE>
class tFifo
{
  public:
    tFifo() // constructor
    {
        Init();
    }

    void Init(void)
    {
        writepos = readpos = 0;
        SIZEMASK = FIFO_SIZE - 1;
        overflow_count = 0;
    }

    bool Put(T c)
    {
        uint16_t next = (writepos + 1) & (SIZEMASK);
        if (next != readpos) { // fifo not full
            buf[writepos] = c;
            writepos = next;
            return true;
        }
        CountOverflow();
        return false;
    }

    bool PutBuf(const void* const data, uint16_t len)
    {
        if (!HasSpace(len)) {
            CountOverflow();
            return false;
        }

        const T* const data_t = (const T*)data;
        for (uint16_t i = 0; i < len; i++) {
            buf[writepos] = data_t[i];
            writepos = (writepos + 1) & SIZEMASK;
        }
        return true;
    }

    uint16_t Available(void)
    {
        int16_t d = (int16_t)writepos - (int16_t)readpos;
        if (d < 0) return d + FIFO_SIZE; // was (SIZEMASK + 1);
        return d;
    }

    bool HasSpace(uint16_t space)
    {
        if (space >= FIFO_SIZE) return false;
        return Available() <= (FIFO_SIZE - 1 - space);
    }

    uint32_t OverflowCount(void) const { return overflow_count; }

    bool IsFull(void)
    {
        return (((writepos + 1) & (SIZEMASK)) == readpos);
    }

    T Get(void)
    {
        if (writepos != readpos) { // fifo not empty
            T c = buf[readpos];
            readpos = (readpos + 1) & (SIZEMASK);
            return c;
        }
        return 0;
    }

    void Flush(void)
    {
        writepos = readpos = 0;
    }

  private:
    void CountOverflow(void)
    {
        if (overflow_count < UINT32_MAX) overflow_count++;
    }

    uint16_t writepos; // pos at which the next byte will be stored
    uint16_t readpos; // pos at which the oldest byte is fetched
    uint16_t SIZEMASK;
    uint32_t overflow_count;
    T buf[FIFO_SIZE];
};


#endif // FIFO_H
