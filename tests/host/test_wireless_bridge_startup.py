from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
BRIDGE_SOURCE = REPO_ROOT / "esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino"


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


class WirelessBridgeStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = BRIDGE_SOURCE.read_text(encoding="utf-8")

    def test_uart_is_validated_before_protocol_handler_initialization(self):
        setup = function_body(self.source, "void setup()")
        steps = (
            "SERIAL.setRxBufferSize",
            "SERIAL.begin",
            "serial_startup_error(",
            "serial_startup_halt(serial_error)",
            "switch (g_protocol)",
        )
        positions = [setup.index(step) for step in steps]
        self.assertEqual(positions, sorted(positions))

    def test_failure_path_is_permanent_and_observable(self):
        halt = function_body(self.source, "void serial_startup_halt(")
        self.assertIn('DBG_PRINTLN((int)error)', halt)
        self.assertIn("while (true)", halt)
        self.assertIn("led_on(false)", halt)
        self.assertIn("led_off()", halt)
        self.assertIn("delay(1000)", halt)


if __name__ == "__main__":
    unittest.main()
