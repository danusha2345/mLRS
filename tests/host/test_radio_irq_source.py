import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
RX_SOURCE = REPO_ROOT / "mLRS/CommonRx/mlrs-rx.cpp"
TX_SOURCE = REPO_ROOT / "mLRS/CommonTx/mlrs-tx.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


class RadioIrqSourceTests(unittest.TestCase):
    def test_radio_isrs_only_publish_pending_events(self):
        forbidden = (
            "GetAndClearIrqStatus",
            "ReadBuffer",
            "ReadFrame",
            "Spi",
            "bind.",
            "Config.",
        )

        for source_path in (RX_SOURCE, TX_SOURCE):
            source = source_path.read_text(encoding="utf-8")
            for signature in (
                "void SX_DIO_EXTI_IRQHandler(void)",
                "void SX2_DIO_EXTI_IRQHandler(void)",
            ):
                with self.subTest(source=source_path.name, function=signature):
                    body = function_body(source, signature)
                    self.assertIn("SetFromIsr()", body)
                    for token in forbidden:
                        self.assertNotIn(token, body)

    def test_sync_mismatch_is_recoverable(self):
        for source_path in (RX_SOURCE, TX_SOURCE):
            source = source_path.read_text(encoding="utf-8")
            body = function_body(source, "uint8_t do_receive(")
            with self.subTest(source=source_path.name):
                self.assertIn("CHECK_ERROR_SYNCWORD", body)
                self.assertIn("radio_recovery_note_error(antenna)", body)
                self.assertNotIn("FAIL_WMSG", body)

    def test_safe_irq_snapshot_only_clears_observed_bits(self):
        expected = {
            "mLRS/Common/sx-drivers/sx128x_driver.h":
                "ClearIrqStatus(irq_status & IrqMask)",
            "mLRS/Common/sx-drivers/sx126x_driver.h":
                "ClearIrqStatus(irq_status & IrqMask)",
            "mLRS/Common/sx-drivers/sx127x_driver.h":
                "ClearIrqStatus(irq_status & IrqMask)",
            "mLRS/Common/sx-drivers/lr11xx_driver.h":
                "ClearIrq(irq_status & IrqToClear)",
        }

        for relative_path, clear_call in expected.items():
            source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            body = function_body(source, "uint32_t GetAndClearIrqStatusSafe(")
            with self.subTest(driver=relative_path):
                self.assertIn(clear_call, body)

    def test_busy_waits_have_a_deadline(self):
        for relative_path in (
            "mLRS/Common/sx-drivers/sx128x_driver.h",
            "mLRS/Common/sx-drivers/sx126x_driver.h",
            "mLRS/Common/sx-drivers/lr11xx_driver.h",
            "mLRS/Common/sx-drivers/lr20xx_driver.h",
        ):
            source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            with self.subTest(driver=relative_path):
                self.assertEqual(source.count("SX_BUSY_TIMEOUT_US"), 2)
                self.assertGreaterEqual(source.count("SetBusyTimeout()"), 2)


if __name__ == "__main__":
    unittest.main()
