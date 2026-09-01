import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware"


class FirmwareSourceStructureTests(unittest.TestCase):
    def test_production_sources_never_include_c_files(self):
        include_c = re.compile(r'^\s*#\s*include\s+"[^"]+\.c"', re.MULTILINE)
        offenders = []
        for path in sorted(FIRMWARE.rglob("*")):
            if path.suffix not in {".c", ".h"}:
                continue
            if include_c.search(path.read_text(encoding="utf-8")):
                offenders.append(path.relative_to(ROOT).as_posix())
        self.assertEqual([], offenders)

    def test_entry_point_and_runtime_policy_are_separate(self):
        main_source = (FIRMWARE / "main.c").read_text(encoding="utf-8")
        self.assertIn("rp86_canonical_runtime_run", main_source)
        self.assertLessEqual(len(main_source.splitlines()), 20)
        self.assertTrue((FIRMWARE / "runtime" / "canonical_runtime.c").is_file())
        self.assertTrue((FIRMWARE / "bus" / "prepared_responder.c").is_file())

    def test_host_service_policy_is_outside_canonical_runtime(self):
        runtime_source = (
            FIRMWARE / "runtime" / "canonical_runtime.c"
        ).read_text(encoding="utf-8")
        dispatch_source = (
            FIRMWARE / "runtime" / "host_service_dispatch.c"
        ).read_text(encoding="utf-8")
        cmake_source = (FIRMWARE / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("runtime/host_service_dispatch.c", cmake_source)
        self.assertIn("rp86_host_service_dispatch_memory", runtime_source)
        self.assertIn("rp86_host_service_dispatch_filesystem", runtime_source)
        for policy_call in (
            "rp86_flash_service_handle(",
            "rp86_memory_service_handle(",
            "rp86_memory_service_set_write_window(",
        ):
            self.assertNotIn(policy_call, runtime_source)
            self.assertIn(policy_call, dispatch_source)

    def test_non_realtime_service_state_has_one_owner(self):
        runtime_source = (
            FIRMWARE / "runtime" / "canonical_runtime.c"
        ).read_text(encoding="utf-8")
        context_header = (
            FIRMWARE / "runtime" / "runtime_context.h"
        ).read_text(encoding="utf-8")
        context_source = (
            FIRMWARE / "runtime" / "runtime_context.c"
        ).read_text(encoding="utf-8")

        self.assertIn("rp86_runtime_context_t g_runtime", runtime_source)
        self.assertIn("rp86_runtime_context_init(&g_runtime", runtime_source)
        for former_global in (
            "g_workload_manager",
            "g_workload_memory",
            "g_memory_service",
            "g_flash_volume",
            "g_flash_service",
            "g_host_services",
        ):
            self.assertNotIn(former_global, runtime_source)
        self.assertIn("rp86_workload_manager_t workload", context_header)
        self.assertIn("rp86_host_service_dispatch_t host_services", context_header)
        self.assertIn("rp86_shared_mailbox_init", context_source)

    def test_general_workload_execution_has_one_owner(self):
        runtime_source = (
            FIRMWARE / "runtime" / "canonical_runtime.c"
        ).read_text(encoding="utf-8")
        executor_source = (
            FIRMWARE / "runtime" / "workload_executor.c"
        ).read_text(encoding="utf-8")
        executor_header = (
            FIRMWARE / "runtime" / "workload_executor.h"
        ).read_text(encoding="utf-8")
        cmake_source = (FIRMWARE / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("runtime/workload_executor.c", cmake_source)
        for delegated_call in (
            "rp86_workload_executor_start(",
            "rp86_workload_executor_stop(",
            "rp86_workload_executor_service(",
        ):
            self.assertIn(delegated_call, runtime_source)
        for execution_detail in (
            "build_reset_handoff",
            "rp86_clock_stepped_service_cycle",
            "GENERAL_BUS_STARVATION_TIMEOUT_US",
        ):
            self.assertNotIn(execution_detail, runtime_source)
            self.assertIn(execution_detail, executor_source)
        self.assertIn("rp86_workload_executor_t", executor_header)

    def test_superseded_parallel_runtime_is_absent(self):
        self.assertFalse((FIRMWARE / "runtime" / "runtime.c").exists())
        self.assertFalse((FIRMWARE / "runtime" / "runtime.h").exists())


if __name__ == "__main__":
    unittest.main()
