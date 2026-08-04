import importlib.util
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_SCRIPT = REPO_ROOT / "tools/run_make_firmwares.py"


def load_build_script():
    spec = importlib.util.spec_from_file_location("run_make_firmwares", BUILD_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Stm32BuildFailureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = load_build_script()

    def test_run_checked_rejects_nonzero_exit_code(self):
        with mock.patch.object(self.build.subprocess, "call", return_value=7):
            with self.assertRaisesRegex(
                RuntimeError,
                r"compile broken\.c failed with exit code 7",
            ):
                self.build.run_checked("fake-compiler broken.c", "compile broken.c")

    def test_run_checked_accepts_zero_exit_code(self):
        with mock.patch.object(self.build.subprocess, "call", return_value=0):
            self.build.run_checked("fake-compiler ok.c", "compile ok.c")

    def test_artifact_must_exist_and_be_nonempty(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            artifact = Path(temp_dir) / "firmware.elf"

            with self.assertRaisesRegex(RuntimeError, "did not create artifact"):
                self.build.require_nonempty_file(str(artifact), "link rx-test")

            artifact.touch()
            with self.assertRaisesRegex(RuntimeError, "created empty artifact"):
                self.build.require_nonempty_file(str(artifact), "link rx-test")

            artifact.write_bytes(b"firmware")
            self.build.require_nonempty_file(str(artifact), "link rx-test")

    def test_parallel_compile_failure_prevents_link(self):
        target = types.SimpleNamespace(
            build_dir="rx-test",
            extra_D_list=[],
            MLRS_SOURCES_CORE=[],
            MLRS_SOURCES_EXTRA=[],
            MLRS_SOURCES_HAL=[],
            rx_or_tx="rx",
            startup_script="startup.s",
            target="rx-test",
        )

        with mock.patch.multiple(
            self.build,
            MLRS_SOURCES_COMMON=[],
            MLRS_SOURCES_MODULES=[],
            MLRS_SOURCES_RX=[],
            create_clean_dir=mock.DEFAULT,
            create_dir=mock.DEFAULT,
            mlrs_compile_file=mock.DEFAULT,
            mlrs_link_target=mock.DEFAULT,
        ) as patched:
            patched["mlrs_compile_file"].side_effect = RuntimeError("compile failed")

            with self.assertRaisesRegex(RuntimeError, "compile failed"):
                self.build.mlrs_build_target(target, [])

            patched["mlrs_link_target"].assert_not_called()


if __name__ == "__main__":
    unittest.main()
