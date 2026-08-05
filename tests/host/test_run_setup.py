import builtins
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
SETUP_SCRIPT = REPO_ROOT / "run_setup.py"


def load_setup_script():
    spec = importlib.util.spec_from_file_location("run_setup", SETUP_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class RunSetupAutomationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.setup = load_setup_script()

    def test_generator_uses_current_python_interpreter(self):
        self.setup.silent = False
        with mock.patch.object(self.setup.os, "chdir"), mock.patch.object(
            self.setup.subprocess,
            "call",
            return_value=0,
        ) as subprocess_call:
            self.setup.generate_mavlink_c_library()

        command = subprocess_call.call_args.args[0]
        self.assertEqual(command[0], sys.executable)

    def test_child_failure_returns_nonzero_without_noninteractive_pause(self):
        self.setup.pause_on_exit = False
        self.setup.silent = False
        with mock.patch.object(
            self.setup.subprocess,
            "call",
            return_value=9,
        ), mock.patch.object(builtins, "input") as user_input:
            with self.assertRaises(SystemExit) as exit_context:
                self.setup.os_system(["broken-command"])

        self.assertEqual(exit_context.exception.code, 1)
        user_input.assert_not_called()

    def test_non_tty_runs_selected_step_without_pause(self):
        fake_stdin = types.SimpleNamespace(isatty=lambda: False)
        with mock.patch.object(self.setup.sys, "stdin", fake_stdin), mock.patch.object(
            self.setup,
            "check_python",
        ), mock.patch.object(self.setup, "git_submodules_update") as submodules, mock.patch.object(
            self.setup,
            "copy_st_drivers",
        ) as copy_drivers, mock.patch.object(
            self.setup,
            "generate_mavlink_c_library",
        ) as generate_mavlink, mock.patch.object(
            self.setup,
            "generate_dronecan_c_library",
        ) as generate_dronecan, mock.patch.object(builtins, "input") as user_input:
            result = self.setup.main(["--mavlink", "--silent"])

        self.assertEqual(result, 0)
        self.assertTrue(self.setup.silent)
        generate_mavlink.assert_called_once_with()
        submodules.assert_not_called()
        copy_drivers.assert_not_called()
        generate_dronecan.assert_not_called()
        user_input.assert_not_called()

    def test_no_pause_overrides_interactive_stdin(self):
        fake_stdin = types.SimpleNamespace(isatty=lambda: True)
        with mock.patch.object(self.setup.sys, "stdin", fake_stdin), mock.patch.object(
            self.setup,
            "check_python",
        ), mock.patch.object(
            self.setup,
            "generate_mavlink_c_library",
        ), mock.patch.object(builtins, "input") as user_input:
            result = self.setup.main(["--mavlink", "--no-pause"])

        self.assertEqual(result, 0)
        user_input.assert_not_called()


if __name__ == "__main__":
    unittest.main()
