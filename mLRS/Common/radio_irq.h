//*******************************************************
// Copyright (c) MLRS project
// GPL3
// https://www.gnu.org/licenses/gpl-3.0.de.html
//*******************************************************
// Radio IRQ handoff and recovery helpers
//*******************************************************
#ifndef RADIO_IRQ_H
#define RADIO_IRQ_H
#pragma once

#include <stdint.h>


#define RADIO_ERROR_REINIT_THRESHOLD  3
#define RADIO_REINIT_RETRY_DELAY_MS   1000


inline bool radio_recovery_deadline_reached(uint32_t now_ms, uint32_t retry_at_ms)
{
    return (int32_t)(now_ms - retry_at_ms) >= 0;
}


#ifdef ESP32
static portMUX_TYPE radio_irq_pending_mux = portMUX_INITIALIZER_UNLOCKED;
#endif


class tRadioIrqPending
{
  public:
    void Init(void)
    {
        pending_count = 0;
    }

    inline __attribute__((always_inline)) void SetFromIsr(void)
    {
#ifdef ESP32
        portENTER_CRITICAL_ISR(&radio_irq_pending_mux);
#endif
        if (pending_count < UINT8_MAX) pending_count++;
#ifdef ESP32
        portEXIT_CRITICAL_ISR(&radio_irq_pending_mux);
#endif
    }

    uint8_t Take(void)
    {
#ifdef ESP32
        portENTER_CRITICAL(&radio_irq_pending_mux);
#elif defined ESP8266
        noInterrupts();
#elif !defined RADIO_IRQ_HOST_TEST
        __disable_irq();
#endif

        uint8_t count = pending_count;
        pending_count = 0;

#ifdef ESP32
        portEXIT_CRITICAL(&radio_irq_pending_mux);
#elif defined ESP8266
        interrupts();
#elif !defined RADIO_IRQ_HOST_TEST
        __enable_irq();
#endif
        return count;
    }

  private:
    volatile uint8_t pending_count;
};


class tRadioErrorTracker
{
  public:
    void Init(void)
    {
        total_count = 0;
        consecutive_count = 0;
    }

    bool NoteError(void)
    {
        if (total_count < UINT32_MAX) total_count++;
        if (consecutive_count < UINT8_MAX) consecutive_count++;

        if (consecutive_count < RADIO_ERROR_REINIT_THRESHOLD) return false;

        consecutive_count = 0;
        return true;
    }

    void NoteSuccess(void)
    {
        consecutive_count = 0;
    }

    uint32_t TotalCount(void) const { return total_count; }
    uint8_t ConsecutiveCount(void) const { return consecutive_count; }

  private:
    uint32_t total_count;
    uint8_t consecutive_count;
};


#endif // RADIO_IRQ_H
