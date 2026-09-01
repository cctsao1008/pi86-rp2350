from datetime import datetime, timezone
import json
from pathlib import Path
import tempfile
import sys
import unittest


TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime.session_evidence import SessionEvidence  # noqa: E402


class SessionEvidenceTests(unittest.TestCase):
    def test_writes_raw_transport_and_structured_session(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            evidence = SessionEvidence.create(
                Path(directory),
                datetime(2026, 9, 1, 3, 30, tzinfo=timezone.utc),
            )
            evidence.capture(b"physical evidence\r\n")
            evidence.record({"sequence": 7, "passed": True})

            evidence.write({"schema": "rp86.runtime-session/v1"})

            self.assertEqual(
                evidence.raw_path.read_bytes(), b"physical evidence\r\n"
            )
            document = json.loads(evidence.json_path.read_text(encoding="utf-8"))
            self.assertEqual(document["events"], [{"sequence": 7, "passed": True}])
            self.assertEqual(
                Path(document["raw_cdc_log"]), evidence.raw_path.resolve()
            )


if __name__ == "__main__":
    unittest.main()
