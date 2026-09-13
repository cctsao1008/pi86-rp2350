from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RepositoryStructureTests(unittest.TestCase):
    def test_legacy_paths_are_absent(self) -> None:
        legacy_paths = [
            "tools/rp86_runtime",
            "tools/rp86.py",
            "tools/rp86_web.py",
            "tools/rp86_web_api.py",
            "tools/rp86_web_view.py",
            "tools/rp86_public.py",
            "tools/rp86_freertos_status.py",
            "tests/host_runtime",
            "tests/runtime",
            "tests/ia16_lab",
            "tests/execution_clock_runtime",
            "tests/docs",
            "docs/architecture.md",
            "docs/host_runtime_architecture.md",
            "docs/host_runtime_shell.md",
            "docs/memory_architecture.md",
            "docs/host_protocol.md",
            "docs/companion_service_abi.md",
            "docs/processor_memory_map.md",
            "docs/processor_io_interrupt_map.md",
            "docs/hardware.md",
            "docs/bringup.md",
        ]
        present = [path for path in legacy_paths if (ROOT / path).exists()]
        self.assertEqual([], present, f"legacy paths still present: {present}")

    def test_live_text_does_not_reference_legacy_paths(self) -> None:
        forbidden = [
            "tools/rp86_runtime",
            "tools/rp86.py",
            r"tools\\rp86.py",
            "tools/rp86_web.py",
            "tools/rp86_web_api.py",
            "tools/rp86_web_view.py",
            "tools/rp86_public.py",
            "tools/rp86_freertos_status.py",
            "tests/host_runtime",
            "tests/ia16_lab",
            "tests/execution_clock_runtime",
            "docs/host_runtime_architecture.md",
            "docs/memory_architecture.md",
            "docs/host_protocol.md",
            "docs/companion_service_abi.md",
            "docs/processor_memory_map.md",
            "docs/processor_io_interrupt_map.md",
        ]
        text_suffixes = {
            ".c", ".h", ".asm", ".inc", ".py", ".md", ".txt", ".json",
            ".yml", ".yaml", ".cmake", ".sh", ".ps1", ".cmd",
        }
        excluded_roots = {".git", ".tools", "third_party", "artifacts"}
        failures: list[str] = []

        for path in ROOT.rglob("*"):
            if not path.is_file() or path == Path(__file__).resolve():
                continue
            relative = path.relative_to(ROOT)
            if relative.parts and relative.parts[0] in excluded_roots:
                continue
            if path.name != "CMakeLists.txt" and path.suffix.lower() not in text_suffixes:
                continue
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            for token in forbidden:
                if token in text:
                    failures.append(f"{relative}: {token}")

        self.assertEqual([], failures, "legacy references remain:\n" + "\n".join(failures))

    def test_tools_contains_engineering_utilities_only(self) -> None:
        allowed = {"diagnostics", "ia16_lab"}
        actual = {entry.name for entry in (ROOT / "tools").iterdir()}
        self.assertEqual(allowed, actual)


if __name__ == "__main__":
    unittest.main()
