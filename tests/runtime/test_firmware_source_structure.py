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

    def test_prepared_runtime_lifecycle_and_identity_have_one_owner(self):
        runtime_source = (
            FIRMWARE / "runtime" / "canonical_runtime.c"
        ).read_text(encoding="utf-8")
        prepared_source = (
            FIRMWARE / "runtime" / "prepared_runtime.c"
        ).read_text(encoding="utf-8")
        prepared_header = (
            FIRMWARE / "runtime" / "prepared_runtime.h"
        ).read_text(encoding="utf-8")
        cmake_source = (FIRMWARE / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("runtime/prepared_runtime.c", cmake_source)
        self.assertIn("rp86_prepared_runtime_t g_prepared_runtime", runtime_source)
        self.assertNotIn("g_prepared_runtime_available", runtime_source)
        self.assertNotIn("g_prepared_runtime_initialized", runtime_source)
        for identity_policy in (
            "RP86_PROCESSOR_SIGNATURE_INTEL_8086",
            "RP86_PROCESSOR_SIGNATURE_NEC_V30",
            "rp86_prepared_processor_identity_name",
            "rp86_prepared_processor_witness_flags",
        ):
            self.assertIn(identity_policy, prepared_header + prepared_source)
        self.assertIn("rp86_prepared_runtime_observe_processor", runtime_source)

        # The PIO/DMA/ISR timing kernel deliberately stays contiguous.
        for timing_path in (
            "run_live_round",
            "companion_dma_irq0",
            "rearm_exact_stream",
        ):
            self.assertIn(timing_path, runtime_source)
            self.assertNotIn(timing_path, prepared_source)

    def test_runtime_status_has_one_typed_snapshot(self):
        runtime_source = (
            FIRMWARE / "runtime" / "canonical_runtime.c"
        ).read_text(encoding="utf-8")
        status_source = (
            FIRMWARE / "runtime" / "runtime_status.c"
        ).read_text(encoding="utf-8")
        status_header = (
            FIRMWARE / "runtime" / "runtime_status.h"
        ).read_text(encoding="utf-8")
        cmake_source = (FIRMWARE / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("runtime/runtime_status.c", cmake_source)
        self.assertIn("rp86_runtime_status_snapshot_t", status_header)
        self.assertIn("rp86_runtime_status_capture", status_source)
        self.assertIn("runtime_status_snapshot()", runtime_source)
        self.assertIn("sizeof snapshot.workload", runtime_source)

    def test_workload_result_fills_one_protocol_payload(self):
        protocol = (
            FIRMWARE / "runtime" / "workload_protocol.h"
        ).read_text(encoding="utf-8")
        executor = (
            FIRMWARE / "runtime" / "workload_executor.c"
        ).read_text(encoding="utf-8")
        self.assertIn("rp86_workload_result_payload_t", protocol)
        self.assertIn("sizeof(rp86_workload_result_payload_t) == 52u", protocol)
        self.assertIn("RP86_WORKLOAD_COMPLETION_NATIVE_HLT", executor)
        self.assertIn('strcmp(executor->diagnostic_line, "RESULT: PASS")', executor)

    def test_idle_workload_status_bypasses_timing_evidence_queue(self):
        runtime = (
            FIRMWARE / "runtime" / "canonical_runtime.c"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "rp86_workload_executor_active(&g_workload_executor) &&\n"
            "        !rp86_workload_executor_processor_idle(&g_workload_executor)",
            runtime,
        )

    def test_superseded_parallel_runtime_is_absent(self):
        self.assertFalse((FIRMWARE / "runtime" / "runtime.c").exists())
        self.assertFalse((FIRMWARE / "runtime" / "runtime.h").exists())


if __name__ == "__main__":
    unittest.main()
