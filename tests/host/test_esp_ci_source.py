from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/esp-builds.yml"
STM32_WORKFLOW = REPO_ROOT / ".github/workflows/stm32-toolchains.yml"
BRIDGE_CONFIG = REPO_ROOT / "esp/mlrs-wireless-bridge/platformio.ini"
TOOL_REQUIREMENTS = REPO_ROOT / "requirements-tools.txt"


class EspCiSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")
        cls.stm32_workflow = STM32_WORKFLOW.read_text(encoding="utf-8")
        cls.bridge_config = BRIDGE_CONFIG.read_text(encoding="utf-8")
        cls.tool_requirements = TOOL_REQUIREMENTS.read_text(encoding="utf-8")

    def test_workflow_runs_on_pull_requests_with_read_only_permissions(self):
        self.assertIn("pull_request:", self.workflow)
        self.assertIn("permissions:\n  contents: read", self.workflow)
        esp_pr_trigger = self.workflow.split("pull_request:", 1)[1].split("push:", 1)[0]
        stm32_pr_trigger = self.stm32_workflow.split("pull_request:", 1)[1].split(
            "push:", 1
        )[0]
        self.assertNotIn("paths:", esp_pr_trigger)
        self.assertNotIn("paths:", stm32_pr_trigger)

    def test_firmware_job_uses_fail_fast_runner_and_exact_core(self):
        self.assertIn("platformio==6.1.19", self.workflow)
        self.assertIn("intelhex==2.3.0", self.tool_requirements)
        self.assertIn("tools/run_make_esp_firmwares.py", self.workflow)
        self.assertIn("--version v1.4.03-ci --nopause", self.workflow)
        self.assertIn('wc -l)" -eq 33', self.workflow)

    def test_bridge_job_builds_all_at_mode_variants(self):
        for environment in (
            "bridge-esp8266-at",
            "bridge-esp32-pico-at",
            "bridge-esp32c3-at",
        ):
            with self.subTest(environment=environment):
                self.assertIn(environment, self.workflow)
                self.assertIn("[env:%s]" % environment, self.bridge_config)

    def test_bridge_toolchains_and_no_ota_partition_are_pinned(self):
        self.assertIn("espressif8266@4.2.1", self.bridge_config)
        self.assertIn("55.03.38-1/platform-espressif32.zip", self.bridge_config)
        self.assertIn("board_build.partitions = no_ota.csv", self.bridge_config)
        self.assertIn("vshymanskyy/Preferences@2.2.2", self.bridge_config)


if __name__ == "__main__":
    unittest.main()
