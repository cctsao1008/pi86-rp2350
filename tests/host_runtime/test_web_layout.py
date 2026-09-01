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
        self.assertNotIn("runtime-only", html)
        self.assertNotIn("overviewTab", html)
        self.assertNotIn("runtimeTab", html)

    def test_labels_native_input_honestly(self) -> None:
        html = rp86_web.INDEX_HTML
        self.assertIn("Native workload input", html)
        self.assertNotIn("Physical processor console", html)
        self.assertNotIn("Processor console", html)
        self.assertIn("s.native_command_available===true", html)
        self.assertIn("new TextEncoder()", html)
        self.assertIn("14 UTF-8 bytes", html)

    def test_responsive_header_avoids_sticky_overlap(self) -> None:
        html = rp86_web.INDEX_HTML
        self.assertIn("@media(max-width:1100px){header,.statebar{position:static}", html)
        self.assertNotIn(".statebar{top:126px}", html)
        self.assertIn('class="statebar" aria-live="polite"', html)

    def test_memory_inputs_have_visible_labels(self) -> None:
        html = rp86_web.INDEX_HTML
        self.assertIn('for="memoryAddress">Processor address</label>', html)
        self.assertIn('for="memoryLength">Bytes</label>', html)
        self.assertIn('id="memoryOut" class="memory-output" aria-live="polite"', html)
        self.assertIn("grid-template-columns:minmax(260px,520px) 90px auto", html)
        self.assertIn("@media(max-width:760px){.memory-line{grid-template-columns:1fr}}", html)

    def test_http_server_delegates_to_api_boundary(self) -> None:
        source = (TOOLS / "rp86_web.py").read_text(encoding="utf-8")
        handler = source[source.index("class Handler"):]
        self.assertIn("API.get(path)", handler)
        self.assertIn("API.post(path, payload)", handler)
        self.assertIn("API.ensure_runtime_owner()", handler)
        self.assertIn("API.stop_owned_runtime()", handler)
        self.assertNotIn("BrokerClient(", handler)

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


if __name__ == "__main__":
    unittest.main()
