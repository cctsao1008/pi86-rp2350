from __future__ import annotations

import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import rp86_web  # noqa: E402


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


if __name__ == "__main__":
    unittest.main()
