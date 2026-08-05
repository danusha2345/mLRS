from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


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


class ParserBudgetSourceTests(unittest.TestCase):
    def test_all_unbounded_parser_loops_consume_a_budget(self):
        expected_uses = {
            "mLRS/CommonTx/mavlink_interface_tx.h": {
                "void tTxMavlink::parse_serial_in_link_out(void)": 3,
                "void tTxMavlink::parse_link_in_serial_out(void)": 1,
            },
            "mLRS/CommonRx/mavlink_interface_rx.h": {
                "void tRxMavlink::parse_serial_in_link_out(void)": 1,
            },
            "mLRS/CommonTx/msp_interface_tx.h": {
                "void tTxMsp::parse_serial_in_link_out(void)": 1,
            },
            "mLRS/CommonRx/msp_interface_rx.h": {
                "void tRxMsp::parse_serial_in_link_out(void)": 1,
            },
        }

        for relative_path, functions in expected_uses.items():
            source = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            with self.subTest(source=relative_path):
                self.assertIn('../Common/libs/parser_budget.h', source)
            for signature, expected_count in functions.items():
                with self.subTest(source=relative_path, function=signature):
                    body = function_body(source, signature)
                    self.assertIn("tParserByteBudget budget;", body)
                    self.assertEqual(body.count("budget.Take()"), expected_count)


if __name__ == "__main__":
    unittest.main()
