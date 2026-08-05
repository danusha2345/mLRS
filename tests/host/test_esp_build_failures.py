import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_SCRIPT = REPO_ROOT / "tools/run_make_esp_firmwares.py"


def load_build_script():
    spec = importlib.util.spec_from_file_location("run_make_esp_firmwares", BUILD_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class EspBuildFailureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = load_build_script()

    def make_project(self, root, environments=("rx-one", "tx-two")):
        project_dir = Path(root)
        (project_dir / "mLRS/Common").mkdir(parents=True)
        (project_dir / "mLRS/Common/common_conf.h").write_text(
            '#define VERSIONONLYSTR "1.4.02"\n', encoding="utf-8"
        )
        sections = "\n".join("[env:%s]\nboard = test" % env for env in environments)
        (project_dir / "platformio.ini").write_text(sections + "\n", encoding="utf-8")
        return project_dir

    def test_cli_help_does_not_probe_platformio(self):
        result = subprocess.run(
            [sys.executable, str(BUILD_SCRIPT), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0)
        self.assertIn("--platformio", result.stdout)
        self.assertNotIn("PlatformIO =", result.stdout)

    def test_cli_rejects_unknown_and_missing_options(self):
        unknown = subprocess.run(
            [sys.executable, str(BUILD_SCRIPT), "--unknown-option"],
            capture_output=True,
            text=True,
            check=False,
        )
        missing = subprocess.run(
            [sys.executable, str(BUILD_SCRIPT), "--target"],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(unknown.returncode, 2)
        self.assertIn("unrecognized arguments", unknown.stderr)
        self.assertEqual(missing.returncode, 2)
        self.assertIn("expected one argument", missing.stderr)

    def test_cli_preserves_aliases_and_repeatable_defines(self):
        args = self.build.parse_arguments([
            "-T", "rx-one", "-D", "FIRST", "-d", "SECOND=2",
            "-np", "-V", "1.2.3", "--platformio", "/opt/pio",
        ])

        self.assertEqual(args.target, "rx-one")
        self.assertEqual(args.define, ["FIRST", "SECOND=2"])
        self.assertTrue(args.nopause)
        self.assertEqual(args.version, "1.2.3")
        self.assertEqual(args.platformio, "/opt/pio")

    def test_defines_append_to_existing_platformio_flags(self):
        with mock.patch.dict(os.environ, {"PLATFORMIO_BUILD_FLAGS": "-DEXISTING"}):
            environment = self.build.build_environment(["FIRST", "SECOND=2"])

        self.assertEqual(
            environment["PLATFORMIO_BUILD_FLAGS"],
            "-DEXISTING\n-DFIRST\n-DSECOND=2",
        )

    def test_unknown_target_fails_before_tool_lookup_or_cleanup(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            project_dir = self.make_project(temp_dir)
            args = self.build.parse_arguments([
                "--target", "rx-typo", "--version", "1.4.02", "--nopause",
            ])
            with mock.patch.object(self.build, "resolve_platformio") as resolve:
                with mock.patch.object(self.build, "remove_build_directories") as clean:
                    with self.assertRaisesRegex(self.build.UsageError, "unknown PlatformIO"):
                        self.build.execute(
                            args,
                            project_dir=project_dir,
                            build_dir=project_dir / ".pio/build",
                            output_dir=project_dir / "tools/esp-build",
                        )

        resolve.assert_not_called()
        clean.assert_not_called()

    def test_missing_tool_is_reported(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            missing = Path(temp_dir) / "missing-pio"
            with self.assertRaisesRegex(FileNotFoundError, "PlatformIO executable"):
                self.build.resolve_platformio(str(missing))

    def test_build_failure_does_not_publish(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            project_dir = self.make_project(temp_dir, ("rx-one",))
            build_dir = project_dir / ".pio/build"
            output_dir = project_dir / "tools/esp-build"
            published = output_dir / "firmware"
            published.mkdir(parents=True)
            sentinel = published / "keep.bin"
            sentinel.write_bytes(b"previous release")
            args = self.build.parse_arguments([
                "--target", "rx-one", "--version", "1.4.02", "--nopause",
            ])
            results = [types.SimpleNamespace(returncode=0), types.SimpleNamespace(returncode=7)]

            with mock.patch.object(self.build, "resolve_platformio", return_value="pio"):
                with mock.patch.object(self.build, "version_suffix", return_value=""):
                    with mock.patch.object(
                        self.build.subprocess, "run", side_effect=results,
                    ) as run:
                        with self.assertRaisesRegex(RuntimeError, "build failed with exit code 7"):
                            self.build.execute(args, project_dir, build_dir, output_dir)

            self.assertEqual(sentinel.read_bytes(), b"previous release")
            self.assertEqual(run.call_count, 2)

    def test_noop_build_cannot_reuse_stale_artifact(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            project_dir = self.make_project(temp_dir, ("rx-one",))
            build_dir = project_dir / ".pio/build"
            stale = build_dir / "rx-one/firmware.bin"
            stale.parent.mkdir(parents=True)
            stale.write_bytes(b"stale firmware")
            output_dir = project_dir / "tools/esp-build"
            published = output_dir / "firmware"
            published.mkdir(parents=True)
            sentinel = published / "keep.bin"
            sentinel.write_bytes(b"previous release")
            args = self.build.parse_arguments([
                "--target", "rx-one", "--version", "1.4.02", "--nopause",
            ])

            with mock.patch.object(self.build, "resolve_platformio", return_value="pio"):
                with mock.patch.object(self.build, "version_suffix", return_value=""):
                    with mock.patch.object(
                        self.build.subprocess,
                        "run",
                        return_value=types.SimpleNamespace(returncode=0),
                    ):
                        with self.assertRaisesRegex(RuntimeError, "was not produced"):
                            self.build.execute(args, project_dir, build_dir, output_dir)

            self.assertFalse(stale.exists())
            self.assertEqual(sentinel.read_bytes(), b"previous release")

    def test_publish_happens_only_after_all_artifacts_validate(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            build_dir = root / "build"
            first = build_dir / "rx-one/firmware.bin"
            first.parent.mkdir(parents=True)
            first.write_bytes(b"one")
            missing = build_dir / "tx-two/firmware.bin"
            output_dir = root / "output"

            with self.assertRaisesRegex(RuntimeError, "tx-two"):
                artifacts = self.build.validate_artifacts(build_dir, ["rx-one", "tx-two"])
                self.build.publish_artifacts(artifacts, output_dir, "1.4.02", "")

            self.assertFalse((output_dir / "firmware").exists())
            self.assertFalse(missing.exists())

    def test_detached_checkout_has_no_dangling_branch_separator(self):
        with mock.patch.object(
            self.build,
            "git_output",
            side_effect=["", "abc12345"],
        ):
            suffix = self.build.version_suffix(REPO_ROOT, "1.4.03")

        self.assertEqual(suffix, "-@abc12345")


if __name__ == "__main__":
    unittest.main()
