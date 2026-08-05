#include <cstdint>
#include <cstdlib>
#include <iostream>

#define RADIO_IRQ_HOST_TEST
#include "mLRS/Common/radio_irq.h"


[[noreturn]] static void fail(const char* message)
{
    std::cerr << "Radio IRQ host test failed: " << message << '\n';
    std::exit(1);
}


static void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}


static void test_pending_events_are_drained_once()
{
    tRadioIrqPending pending;
    pending.Init();

    expect(pending.Take() == 0, "new pending queue was not empty");
    pending.SetFromIsr();
    pending.SetFromIsr();
    pending.SetFromIsr();
    expect(pending.Take() == 3, "pending events were collapsed");
    expect(pending.Take() == 0, "drained events were returned twice");

    pending.SetFromIsr();
    expect(pending.Take() == 1, "event after drain was lost");
}


static void test_pending_counter_saturates()
{
    tRadioIrqPending pending;
    pending.Init();

    for (unsigned i = 0; i < 300; i++) pending.SetFromIsr();
    expect(pending.Take() == UINT8_MAX, "pending counter wrapped instead of saturating");
}


static void test_recovery_threshold_and_success_reset()
{
    tRadioErrorTracker errors;
    errors.Init();

    expect(!errors.NoteError(), "first error requested recovery");
    expect(!errors.NoteError(), "second error requested recovery");
    expect(errors.NoteError(), "third consecutive error did not request recovery");
    expect(errors.TotalCount() == 3, "total error count is wrong");
    expect(errors.ConsecutiveCount() == 0, "threshold did not reset the streak");

    expect(!errors.NoteError(), "new streak requested recovery too early");
    errors.NoteSuccess();
    expect(errors.ConsecutiveCount() == 0, "success did not reset the streak");
    expect(errors.TotalCount() == 4, "success changed the total error count");

    expect(!errors.NoteError(), "post-success first error requested recovery");
    expect(!errors.NoteError(), "post-success second error requested recovery");
    expect(errors.NoteError(), "post-success third error did not request recovery");
}


static void test_recovery_retry_deadline_wraparound()
{
    expect(!radio_recovery_deadline_reached(999, 1000), "future deadline was due early");
    expect(radio_recovery_deadline_reached(1000, 1000), "exact deadline was not due");
    expect(radio_recovery_deadline_reached(1001, 1000), "past deadline was not due");

    constexpr uint32_t retry_at_after_wrap = 500;
    expect(!radio_recovery_deadline_reached(UINT32_MAX - 100, retry_at_after_wrap),
           "wrapped future deadline was due early");
    expect(radio_recovery_deadline_reached(500, retry_at_after_wrap),
           "wrapped deadline was not due");
}


int main()
{
    test_pending_events_are_drained_once();
    test_pending_counter_saturates();
    test_recovery_threshold_and_success_reset();
    test_recovery_retry_deadline_wraparound();
    std::cout << "Radio IRQ host tests passed\n";
    return 0;
}
