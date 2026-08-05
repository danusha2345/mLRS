#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

enum : uint8_t {
    MODE_FLRC_111HZ = 0,
    MODE_FSK_50HZ,
    MODE_50HZ,
    MODE_31HZ,
    MODE_19HZ,
    MODE_19HZ_7X,
};

#include "mLRS/Common/frame_types.h"
#include "mLRS/Common/arq.h"


[[noreturn]] static void fail(const char* message)
{
    std::cerr << "ARQ host test failed: " << message << '\n';
    std::exit(1);
}


static void expect(bool condition, const char* message)
{
    if (!condition) fail(message);
}


static void test_wire_layout_and_ack_roundtrip()
{
    static_assert(sizeof(tTxFrameStatus) == 5, "Tx status wire size changed");
    static_assert(sizeof(tRxFrameStatus) == 5, "Rx status wire size changed");
    static_assert(sizeof(tTxFrame) == 91, "Tx frame wire size changed");
    static_assert(sizeof(tRxFrame) == 91, "Rx frame wire size changed");
    static_assert(FRAME_TX_PAYLOAD_LEN == 64, "Tx payload capacity changed");
    static_assert(FRAME_RX_PAYLOAD_LEN == 82, "Rx payload capacity changed");

    tTxFrameStatus tx_status = {};
    tRxFrameStatus rx_status = {};
    for (uint8_t ack = 0; ack < 8; ack++) {
        txframe_status_set_ack(&tx_status, ack);
        rxframe_status_set_ack(&rx_status, ack);
        expect(txframe_status_ack(&tx_status) == ack, "Tx ACK did not round-trip");
        expect(rxframe_status_ack(&rx_status) == ack, "Rx ACK did not round-trip");
    }

    tx_status.frame_type = frame_type_with_discontinuity(FRAME_TYPE_TX_RX_CMD, true);
    expect(frame_type_value(tx_status.frame_type) == FRAME_TYPE_TX_RX_CMD,
           "discontinuity changed base frame type");
    expect(frame_type_is_arq_v2(tx_status.frame_type),
           "new frame did not carry ARQ protocol version flag");
    expect(frame_type_has_discontinuity(tx_status.frame_type),
           "discontinuity marker was not encoded");
    expect(!frame_type_is_arq_v2(FRAME_TYPE_TX),
           "legacy frame was mistaken for ARQ v2");
}


static void test_stale_ack_cannot_confirm_new_payload()
{
    tTransmitArq tx;
    tReceiveArq rx;
    tx.Init();
    rx.Init();
    tx.SetRetryCnt(1);

    expect(tx.GetFreshPayload(), "first payload was not fresh");
    expect(tx.SeqNo() == 1, "first sequence number was not one");
    expect(!tx.PayloadDiscontinuity(), "first payload was marked discontinuous");

    tx.FrameMissed();
    expect(!tx.GetFreshPayload(), "payload advanced before retry budget was exhausted");
    tx.FrameMissed();
    expect(tx.GetFreshPayload(), "payload did not advance after retry exhaustion");
    expect(tx.SeqNo() == 2, "forced advance did not increment sequence number");
    expect(tx.PayloadDiscontinuity(), "forced advance was not marked discontinuous");

    tx.AckReceived(1, false);
    expect(!tx.GetFreshPayload(), "stale ACK confirmed a new payload");

    rx.Received(tx.SeqNo(), tx.PayloadDiscontinuity());
    expect(rx.AcceptPayload(), "receiver rejected marked payload");
    expect(rx.FrameLost(), "marked payload did not report stream discontinuity");
    tx.AckReceived(rx.AckSeqNo(), rx.AckDiscontinuity());
    expect(tx.GetFreshPayload(), "marked ACK did not release resync payload");
    expect(!tx.PayloadDiscontinuity(), "normal payload kept discontinuity marker");
}


static void test_full_wrap_marker_is_not_a_duplicate()
{
    tReceiveArq rx;
    rx.Init();

    rx.Received(5, false);
    expect(rx.AcceptPayload(), "initial payload was rejected");

    rx.Received(5, false);
    expect(!rx.AcceptPayload(), "normal duplicate was accepted");

    rx.Received(5, true);
    expect(rx.AcceptPayload(), "marked modulo-wrap payload was rejected as duplicate");
    expect(rx.FrameLost(), "marked modulo-wrap did not reset stream parsers");
    expect(rx.AckSeqNo() == 5, "receiver ACK sequence is wrong");
    expect(rx.AckDiscontinuity(), "receiver did not echo discontinuity marker");

    rx.Received(5, true);
    expect(!rx.AcceptPayload(), "marked retransmission was accepted twice");
}


static void test_retry_thresholds()
{
    tTransmitArq tx;
    tx.Init();

    tx.SetRetryCntAuto(699, MODE_FLRC_111HZ);
    expect(tx.payload_retry_cnt == 1, "FLRC 699 threshold is wrong");
    tx.SetRetryCntAuto(700, MODE_FLRC_111HZ);
    expect(tx.payload_retry_cnt == 2, "FLRC 700 threshold is wrong");
    tx.SetRetryCntAuto(799, MODE_FLRC_111HZ);
    expect(tx.payload_retry_cnt == 2, "FLRC 799 threshold is wrong");
    tx.SetRetryCntAuto(800, MODE_FLRC_111HZ);
    expect(tx.payload_retry_cnt == 3, "FLRC 800 threshold is wrong");
    tx.SetRetryCntAuto(799, MODE_31HZ);
    expect(tx.payload_retry_cnt == 1, "LoRa 799 threshold is wrong");
    tx.SetRetryCntAuto(800, MODE_31HZ);
    expect(tx.payload_retry_cnt == 2, "LoRa 800 threshold is wrong");
    tx.SetRetryCntAuto(1000, 255);
    expect(tx.payload_retry_cnt == 1, "unknown mode fallback is wrong");
}


static void test_repeated_resyncs_across_sequence_wraps()
{
    tTransmitArq tx;
    tReceiveArq rx;
    tx.Init();
    rx.Init();
    tx.SetRetryCnt(1);

    uint8_t stale_ack = 0;
    bool stale_discontinuity = false;

    for (unsigned round = 0; round < 12; round++) {
        expect(tx.GetFreshPayload(), "normal payload did not start after resync ACK");
        expect(!tx.PayloadDiscontinuity(), "normal payload was marked during wrap test");
        tx.SetRetryCnt(1);

        tx.FrameMissed();
        expect(!tx.GetFreshPayload(), "normal payload skipped its configured retry");
        tx.FrameMissed();
        expect(tx.GetFreshPayload(), "resync payload was not created after forced drop");
        expect(tx.PayloadDiscontinuity(), "resync payload lost its marker");

        tx.AckReceived(stale_ack, stale_discontinuity);
        expect(!tx.GetFreshPayload(), "stale resync ACK released another marked payload");

        rx.Received(tx.SeqNo(), tx.PayloadDiscontinuity());
        expect(rx.AcceptPayload(), "receiver rejected resync payload across sequence wrap");
        expect(rx.FrameLost(), "resync payload did not report discontinuity");

        stale_ack = rx.AckSeqNo();
        stale_discontinuity = rx.AckDiscontinuity();
        tx.AckReceived(stale_ack, stale_discontinuity);
    }
}


static void test_loss_patterns_never_false_ack()
{
    constexpr unsigned cycles = 8;

    for (unsigned data_mask = 0; data_mask < (1U << cycles); data_mask++) {
        for (unsigned ack_mask = 0; ack_mask < (1U << cycles); ack_mask++) {
            tTransmitArq tx;
            tReceiveArq rx;
            tx.Init();
            rx.Init();
            tx.SetRetryCnt(1);

            int current_payload = -1;
            uint8_t current_seq = 0;
            bool current_discontinuity = false;
            std::vector<bool> accepted(cycles + 1, false);

            for (unsigned cycle = 0; cycle < cycles; cycle++) {
                const bool fresh = tx.GetFreshPayload();
                if (fresh) {
                    if (current_payload >= 0 && !tx.PayloadDiscontinuity()) {
                        expect(accepted[current_payload],
                               "sender advanced after an ACK for an unreceived payload");
                    }
                    current_payload++;
                    current_seq = tx.SeqNo();
                    current_discontinuity = tx.PayloadDiscontinuity();
                    tx.SetRetryCnt(1);
                } else {
                    expect(tx.SeqNo() == current_seq, "retry changed sequence number");
                    expect(tx.PayloadDiscontinuity() == current_discontinuity,
                           "retry changed discontinuity marker");
                }

                if ((data_mask & (1U << cycle)) != 0) {
                    rx.Received(current_seq, current_discontinuity);
                    if (rx.AcceptPayload()) {
                        expect(!accepted[current_payload], "payload was accepted twice");
                        accepted[current_payload] = true;
                    }
                } else {
                    rx.FrameMissed();
                }

                if ((ack_mask & (1U << cycle)) != 0) {
                    tx.AckReceived(rx.AckSeqNo(), rx.AckDiscontinuity());
                } else {
                    tx.FrameMissed();
                }
            }
        }
    }
}


int main()
{
    test_wire_layout_and_ack_roundtrip();
    test_stale_ack_cannot_confirm_new_payload();
    test_full_wrap_marker_is_not_a_duplicate();
    test_retry_thresholds();
    test_repeated_resyncs_across_sequence_wraps();
    test_loss_patterns_never_false_ack();
    std::cout << "ARQ host tests: OK\n";
    return 0;
}
