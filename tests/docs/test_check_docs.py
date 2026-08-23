from pathlib import Path
import tempfile
import unittest

from tools.docs.check_docs import check_repository


class DocumentationCheckerTests(unittest.TestCase):
    def make_root(self) -> tuple[tempfile.TemporaryDirectory[str], Path]:
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "docs").mkdir()
        return temporary, root

    def test_valid_document_passes(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "README.md").write_text("# Project\n\n[Docs](docs/guide.md)\n", encoding="utf-8")
        (root / "docs/guide.md").write_text("# Guide\n\n```text\nok\n```\n", encoding="utf-8")
        failures, waived = check_repository(root)
        self.assertEqual([], failures)
        self.assertEqual([], waived)

    def test_broken_link_fails(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "README.md").write_text("# Project\n\n[Missing](docs/missing.md)\n", encoding="utf-8")
        failures, _ = check_repository(root)
        self.assertIn("MD001", {issue.code for issue in failures})

    def test_unclosed_fence_and_duplicate_heading_fail(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "README.md").write_text("# Project\n\n## Same\n\n## Same\n\n```text\n", encoding="utf-8")
        failures, _ = check_repository(root)
        self.assertEqual({"MD002", "MD003"}, {issue.code for issue in failures})

    def test_positive_ai_current_cycle_claim_fails(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "docs/architecture.md").write_text(
            "# Architecture\n\nCodex answers the current-cycle V30 request.\n", encoding="utf-8"
        )
        failures, _ = check_repository(root)
        self.assertIn("ARCH001", {issue.code for issue in failures})

    def test_explicit_ai_current_cycle_exclusion_passes(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        (root / "docs/architecture.md").write_text(
            "# Architecture\n\nCodex never answers the current-cycle V30 request.\n", encoding="utf-8"
        )
        failures, _ = check_repository(root)
        self.assertNotIn("ARCH001", {issue.code for issue in failures})

    def test_ai_validation_requires_response_architecture(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        validation = root / "docs/validation/ai_b_test.md"
        validation.parent.mkdir()
        validation.write_text(
            "# AI-B test\n\nTarget: test\nClock: stepped\nResult: PASS\nConclusion: bounded.\n",
            encoding="utf-8",
        )
        failures, _ = check_repository(root)
        self.assertIn("VAL005", {issue.code for issue in failures})

    def test_ai_validation_with_response_architecture_passes(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        validation = root / "docs/validation/ai_b_test.md"
        validation.parent.mkdir()
        validation.write_text(
            "# AI-B test\n\n"
            "Target: test\nClock: stepped\nResult: PASS\n"
            "Response architecture: fixed / prestaged\n"
            "Conclusion: bounded.\n",
            encoding="utf-8",
        )
        failures, _ = check_repository(root)
        self.assertNotIn("VAL005", {issue.code for issue in failures})

    def test_ai_validation_rejects_unknown_response_architecture(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        validation = root / "docs/validation/ai_b_test.md"
        validation.parent.mkdir()
        validation.write_text(
            "# AI-B test\n\n"
            "Target: test\nClock: stepped\nResult: PASS\n"
            "Response architecture: TBD\n"
            "Conclusion: bounded.\n",
            encoding="utf-8",
        )
        failures, _ = check_repository(root)
        self.assertIn("VAL005", {issue.code for issue in failures})

    def test_validation_policy_can_be_explicitly_waived(self) -> None:
        temporary, root = self.make_root()
        self.addCleanup(temporary.cleanup)
        validation = root / "docs/validation/legacy.md"
        validation.parent.mkdir()
        validation.write_text("# Legacy\n", encoding="utf-8")
        (root / "docs/document_policy_exceptions.txt").write_text(
            "docs/validation/legacy.md VAL001 historical-record\n"
            "docs/validation/legacy.md VAL002 historical-record\n"
            "docs/validation/legacy.md VAL003 historical-record\n"
            "docs/validation/legacy.md VAL004 historical-record\n",
            encoding="utf-8",
        )
        failures, waived = check_repository(root)
        self.assertEqual([], failures)
        self.assertEqual(4, len(waived))


if __name__ == "__main__":
    unittest.main()
