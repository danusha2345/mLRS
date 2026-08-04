import contextlib
import io
import os
from pathlib import Path
import runpy
import sys
import tempfile
import types
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATOR_SCRIPT = REPO_ROOT / "mLRS/Common/mavlink/fmav_generate_c_library.py"


class FmavGeneratorExitCodeTest(unittest.TestCase):
    def run_generator(self, fmavgen):
        generator_module = types.ModuleType("generator")
        generator_module.fmav_gen = types.SimpleNamespace(
            Opts=lambda *args, **kwargs: object(),
            fmavgen=fmavgen,
        )
        modules_module = types.ModuleType("generator.modules")
        modules_module.fmav_flags = types.SimpleNamespace(
            PARSE_FLAGS_WARNING_ENUM_VALUE_MISSING=1,
        )

        stdout = io.StringIO()
        stderr = io.StringIO()
        exit_code = None
        original_cwd = Path.cwd()
        original_sys_path = sys.path.copy()
        try:
            with tempfile.TemporaryDirectory() as temp_dir, mock.patch.dict(
                sys.modules,
                {
                    "generator": generator_module,
                    "generator.modules": modules_module,
                },
            ), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                os.chdir(temp_dir)
                try:
                    runpy.run_path(str(GENERATOR_SCRIPT), run_name="__main__")
                except SystemExit as ex:
                    exit_code = ex.code
        finally:
            os.chdir(original_cwd)
            sys.path[:] = original_sys_path

        return exit_code, stdout.getvalue(), stderr.getvalue()

    def test_exception_is_reported_and_returns_nonzero(self):
        def fail_generation(*args, **kwargs):
            raise RuntimeError("generator boom")

        exit_code, stdout, stderr = self.run_generator(fail_generation)

        self.assertEqual(exit_code, 1)
        self.assertIn("Error Generating Headers generator boom", stderr)
        self.assertNotIn("Headers generated successfully", stdout)

    def test_success_does_not_raise_system_exit(self):
        exit_code, stdout, stderr = self.run_generator(lambda *args, **kwargs: None)

        self.assertIsNone(exit_code)
        self.assertIn("Headers generated successfully", stdout)
        self.assertEqual(stderr, "")


if __name__ == "__main__":
    unittest.main()
