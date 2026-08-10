import configparser
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class Esp8285RxAsTxSourceTests(unittest.TestCase):
    def test_platformio_target_uses_sx127x_and_explicit_example_define(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini", encoding="utf-8")

        section = config["env:tx-generic-900-rx-as-tx"]
        self.assertIn("sx127x.cpp", section["build_src_filter"])
        self.assertIn("TX_ELRS_GENERIC_900_RX_AS_TX_ESP8285", section["build_flags"])

    def test_hal_documents_verified_pinout_and_post_boot_cli_window(self):
        hal = (ROOT / "mLRS/Common/hal/esp/tx-hal-generic-900-rx-as-tx-esp8285.h").read_text(
            encoding="utf-8"
        )

        for define in (
            "#define DEVICE_HAS_COM_ON_SERIAL",
            "#define SPI_CS_IO                 IO_P15",
            "#define SX_RESET                  IO_P2",
            "#define SX_DIO                    IO_P4",
            "#define BUTTON                    IO_P0",
            "#define LED_RED                   IO_P16",
        ):
            self.assertIn(define, hal)
        self.assertIn("selection_window_ms = 5000", hal)
        self.assertIn("required_press_ms = 250", hal)
        self.assertIn("(elapsed_ms % 100) == 0", hal)
        self.assertNotIn("Serial.available()", hal)

    def test_esp8266_tx_systick_is_one_millisecond(self):
        timer = (ROOT / "mLRS/Common/hal/esp-timer.h").read_text(encoding="utf-8")

        self.assertIn("timer1_write(5000)", timer)
        self.assertNotIn("timer1_write(50);", timer)

    def test_cli_can_switch_shared_uart_back_to_serial_without_reboot(self):
        cli = (ROOT / "mLRS/CommonTx/cli.h").read_text(encoding="utf-8")
        common = (ROOT / "mLRS/Common/common.h").read_text(encoding="utf-8")
        tx = (ROOT / "mLRS/CommonTx/mlrs-tx.cpp").read_text(encoding="utf-8")

        self.assertIn('is_cmd("exit")', cli)
        self.assertIn("ser_or_com_set_to_serial(uint32_t baud)", common)
        self.assertIn("Serials.ser_or_com_set_to_serial(Config.SerialBaudrate)", tx)
        self.assertIn("mavlink.Init(&mbridge)", tx)
        self.assertIn("msp.Init()", tx)
        self.assertIn("sx_serial.Init(&mbridge)", tx)


if __name__ == "__main__":
    unittest.main()
