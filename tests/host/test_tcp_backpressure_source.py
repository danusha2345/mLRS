from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
BRIDGE_SOURCE = REPO_ROOT / "esp/mlrs-wireless-bridge/mlrs-wireless-bridge.ino"


class TcpBackpressureSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = BRIDGE_SOURCE.read_text(encoding="utf-8")
        start = source.index("class tTCPHandler")
        end = source.index("tTCPHandler tcp_handler", start)
        cls.handler = source[start:end]

    def test_tcp_input_loop_has_a_fixed_budget(self):
        self.assertNotIn("while (client.available())", self.handler)
        self.assertIn("tcp_read_to_queue(buf, sizeofbuf)", self.handler)
        self.assertIn("tcp_bridge_transfer_limit(", self.handler)

    def test_both_platforms_use_nonblocking_tcp_capacity(self):
        self.assertIn("client.availableForWrite()", self.handler)
        self.assertIn("client.setTimeout(1)", self.handler)
        self.assertIn("MSG_DONTWAIT", self.handler)
        self.assertIn("errno == EAGAIN || errno == EWOULDBLOCK", self.handler)

    def test_partial_writes_are_consumed_by_actual_length(self):
        self.assertIn("tcp_to_serial_queue.Consume(written)", self.handler)
        self.assertIn("serial_to_tcp_queue.Consume(written_size)", self.handler)
        self.assertIn("tcp_write_partial_count", self.handler)
        self.assertIn("serial_write_partial_count", self.handler)


if __name__ == "__main__":
    unittest.main()
