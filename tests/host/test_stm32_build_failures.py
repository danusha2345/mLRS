import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
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

    def test_cli_help_does_not_probe_toolchain(self):
        result = subprocess.run(
            [sys.executable, str(BUILD_SCRIPT), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("--toolchain-dir", result.stdout)
        self.assertNotIn("toolchain found", result.stdout)

    def test_cli_rejects_unknown_option(self):
        result = subprocess.run(
            [sys.executable, str(BUILD_SCRIPT), "--unknown-option"],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("unrecognized arguments", result.stderr)
        self.assertNotIn("toolchain found", result.stdout)

    def test_cli_rejects_missing_option_value(self):
        result = subprocess.run(
            [sys.executable, str(BUILD_SCRIPT), "--target"],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 2)
        self.assertIn("expected one argument", result.stderr)

    def test_cli_preserves_aliases_and_repeatable_defines(self):
        args = self.build.parse_arguments([
            "-T", "rx-test", "-D", "FIRST", "-d", "SECOND",
            "-np", "-V", "1.2.3", "--toolchain", "/opt/toolchain",
        ])

        self.assertEqual(args.target, "rx-test")
        self.assertEqual(args.define, ["FIRST", "SECOND"])
        self.assertTrue(args.nopause)
        self.assertEqual(args.version, "1.2.3")
        self.assertEqual(args.toolchain_dir, "/opt/toolchain")

    def test_explicit_toolchain_requires_all_programs(self):
        with tempfile.TemporaryDirectory(prefix="toolchain with spaces ") as temp_dir:
            for program in self.build.TOOLCHAIN_PROGRAMS:
                program_path = Path(temp_dir) / program
                program_path.touch()
                os.chmod(program_path, 0o755)

            self.assertEqual(
                self.build.validate_toolchain_dir(temp_dir),
                os.path.abspath(temp_dir),
            )

            (Path(temp_dir) / "arm-none-eabi-objcopy").unlink()
            with self.assertRaisesRegex(ValueError, "arm-none-eabi-objcopy"):
                self.build.validate_toolchain_dir(temp_dir)

            if os.name == "posix":
                previous_gcc_dir = self.build.GCC_DIR
                try:
                    self.build.GCC_DIR = temp_dir
                    command = self.build.toolchain_program("arm-none-eabi-gcc")
                finally:
                    self.build.GCC_DIR = previous_gcc_dir
                self.assertEqual(
                    self.build.shlex.split(command),
                    [str(Path(temp_dir) / "arm-none-eabi-gcc")],
                )

    def test_toolchain_version_is_reported(self):
        completed = types.SimpleNamespace(
            returncode=0,
            stdout="arm-none-eabi-gcc (Arm GNU Toolchain) 11.3.1\nCopyright\n",
        )
        with mock.patch.object(
            self.build.shutil,
            "which",
            return_value="/toolchain/arm-none-eabi-gcc",
        ):
            with mock.patch.object(
                self.build.subprocess,
                "run",
                return_value=completed,
            ):
                version = self.build.report_toolchain_version("/toolchain")

        self.assertEqual(
            version,
            "arm-none-eabi-gcc (Arm GNU Toolchain) 11.3.1",
        )

    def test_zero_matching_targets_fail_before_toolchain_and_cleanup(self):
        targets = [types.SimpleNamespace(target="rx-real-target")]
        with mock.patch.object(
            self.build,
            "mlrs_create_targetlist",
            return_value=targets,
        ):
            with mock.patch.object(
                self.build,
                "resolve_toolchain_dir",
            ) as resolve:
                with mock.patch.object(self.build, "create_clean_dir") as clean:
                    with mock.patch.object(self.build.sys, "stderr", io.StringIO()):
                        result = self.build.main([
                            "--target", "rx-typo", "--version", "1.2.3",
                            "--nopause",
                        ])

        self.assertEqual(result, 2)
        resolve.assert_not_called()
        clean.assert_not_called()

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
