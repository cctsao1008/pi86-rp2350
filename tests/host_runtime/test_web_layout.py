from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import rp86_web  # noqa: E402
from rp86_web_view import processor_view  # noqa: E402


class WebLayoutTests(unittest.TestCase):
    def test_uses_task_oriented_views(self) -> None:
        html = rp86_web.INDEX_HTML
        for tab_id, label in (
            ("operateTab", "Operate"),
            ("inspectTab", "Inspect"),
            ("systemTab", "System"),
        ):
            self.assertIn(f'id="{tab_id}" role="tab">{label}</button>', html)

    def test_labels_native_input_honestly(self) -> None:
        html = rp86_web.INDEX_HTML
        self.assertIn("Native workload input", html)
        self.assertIn("s.native_command_available===true", html)
        self.assertIn("new TextEncoder()", html)
        self.assertIn("14 UTF-8 bytes", html)

    def test_status_changes_are_announced(self) -> None:
        html = rp86_web.INDEX_HTML
        self.assertIn('class="statebar" aria-live="polite"', html)

    def test_memory_inputs_have_visible_labels(self) -> None:
        html = rp86_web.INDEX_HTML
        self.assertIn('for="memoryAddress">Processor address</label>', html)
        self.assertIn('for="memoryLength">Bytes</label>', html)
        self.assertIn('id="memoryOut" class="memory-output" aria-live="polite"', html)

    def test_processor_view_preserves_broker_snapshot(self) -> None:
        record = SimpleNamespace(
            device_id="TEST", processor="auto", tcp_port=1234, udp_port=5678
        )
        snapshot = {"workload_state": "RUNNING", "processor_state": "ACTIVE"}
        view = processor_view(
            owner_mode="existing",
            record=record,
            reply={"ok": True, "processor": "intel-8086", "snapshot": snapshot},
        )
        self.assertEqual(view["snapshot"], snapshot)
        self.assertEqual(view["processor"], "intel-8086")

    def test_workload_card_uses_structured_result_fields(self) -> None:
        html = rp86_web.INDEX_HTML
        for field in (
            "workloadResult",
            "workloadCompletion",
            "workloadOutput",
            "workload_result_structured",
            "workload_completion_reason",
            "workload_native_output",
        ):
            self.assertIn(field, html)


if __name__ == "__main__":
    unittest.main()
