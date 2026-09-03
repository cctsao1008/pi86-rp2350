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

    def test_workload_result_fills_one_protocol_payload(self):
        protocol = (
            FIRMWARE / "runtime" / "workload_protocol.h"
        ).read_text(encoding="utf-8")
        executor = (
            FIRMWARE / "runtime" / "workload_executor.c"
        ).read_text(encoding="utf-8")
        self.assertIn("rp86_workload_result_payload_t", protocol)
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

    def test_boot_identity_does_not_consume_or_reply_to_host_requests(self):
        source = (FIRMWARE / "runtime" / "canonical_runtime.c").read_text(encoding="utf-8")
        boot = source.split("int rp86_canonical_runtime_run(void)", 1)[1].split(
            "/* Persistent service condition:", 1
        )[0]
        self.assertIn("prepare_bootstrap_record();", boot)
        self.assertIn("stage_live_payload(&g_bootstrap_record)", boot)
        self.assertIn("rp86_prepared_runtime_observe_processor(", boot)
        self.assertNotIn("send_live_reply(", boot)
        self.assertNotIn("take_non_control_record(", boot)
        self.assertNotIn("while (!stdio_usb_connected())", boot)


if __name__ == "__main__":
    unittest.main()
