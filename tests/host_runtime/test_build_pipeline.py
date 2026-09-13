import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]


def load_build_driver():
    path = ROOT / "scripts" / "build.py"
    spec = importlib.util.spec_from_file_location("rp86_build_driver", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class BuildPipelineTests(unittest.TestCase):
    def test_public_profiles_are_configuration_level(self):
        build = load_build_driver()
        self.assertEqual(build.COMMANDS, ("firmware", "workloads", "all"))
        self.assertEqual(build.BUILD_DIR["workloads"], "build-workloads")
        self.assertEqual(build.TARGET["workloads"], "rp86_workload_packages")
        self.assertEqual(build.WORKLOAD_STAGE, ROOT / "artifacts" / "workloads")

    def test_package_helpers_share_one_aggregate_and_install_component(self):
        asm = (ROOT / "cmake" / "ProcessorImage.cmake").read_text()
        c16 = (ROOT / "cmake" / "ProcessorC16Image.cmake").read_text()

        self.assertIn("add_custom_target(rp86_workload_packages)", asm)
        self.assertIn("rp86_register_workload_package", asm)
        self.assertIn("rp86_register_workload_package", c16)
        self.assertIn("COMPONENT workloads", asm)
        self.assertNotIn("_package ALL DEPENDS", asm)
        self.assertNotIn("_package ALL DEPENDS", c16)

    def test_build_driver_has_no_per_workload_inventory(self):
        text = (ROOT / "scripts" / "build.py").read_text()
        for name in (
            "TICK.P86W",
            "C16SMOKE.P86W",
            "FREERTOS.P86W",
            "FREERTOS-SYSTEM.P86W",
            "INVSQRT.P86W",
        ):
            self.assertNotIn(name, text)

    def test_clean_stage_removes_stale_artifacts_without_touching_parent(self):
        build = load_build_driver()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / "workloads"
            stage.mkdir()
            (stage / "STALE.P86W").write_bytes(b"stale")
            sentinel = root / "keep.txt"
            sentinel.write_text("keep")

            build.clean_dir(stage)

            self.assertFalse(stage.exists())
            self.assertEqual(sentinel.read_text(), "keep")

    def test_stage_is_cleaned_installed_and_every_package_is_verified(self):
        build = load_build_driver()
        calls = []

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / "workloads"
            stage.mkdir()
            (stage / "STALE.P86W").write_bytes(b"stale")

            def install(build_dir, env):
                calls.append(("install", Path(build_dir)))
                stage.mkdir(parents=True, exist_ok=True)
                (stage / "A.P86W").write_bytes(b"a")
                (stage / "B.P86W").write_bytes(b"b")

            verified = []

            def report(path):
                verified.append(path.name)

            with patch.object(build, "WORKLOAD_STAGE", stage), patch.object(
                build, "cmake_install_workloads", side_effect=install
            ), patch.object(build, "report_workload", side_effect=report):
                build.stage_and_report_workloads(root / "build", {})

            self.assertFalse((stage / "STALE.P86W").exists())
            self.assertEqual(calls, [("install", root / "build")])
            self.assertEqual(verified, ["A.P86W", "B.P86W"])

    def test_all_dispatches_firmware_then_complete_workload_set(self):
        build = load_build_driver()
        calls = []

        def record_firmware(build_dir, target_override, args, base_env):
            calls.append(("firmware", Path(build_dir), target_override))

        def record_workloads(build_dir, target_override, args, base_env):
            calls.append(("workloads", Path(build_dir), target_override))

        with patch.object(build, "build_firmware", side_effect=record_firmware), patch.object(
            build, "build_workloads", side_effect=record_workloads
        ):
            self.assertEqual(build.main(["all"]), 0)

        self.assertEqual(
            calls,
            [
                ("firmware", ROOT / "build-firmware", None),
                ("workloads", ROOT / "build-workloads", None),
            ],
        )


if __name__ == "__main__":
    unittest.main()
