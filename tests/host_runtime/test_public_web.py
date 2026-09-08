from pathlib import Path
import sys
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import rp86_public  # noqa: E402


class PublicSessionTests(unittest.TestCase):
    def test_exclusive_acquire_resume_and_release(self) -> None:
        session = rp86_public.PublicSession()

        first, status = session.acquire(owner="browser-a")
        self.assertEqual(status, 200)
        self.assertTrue(first["ok"])
        token = str(first["token"])
        self.assertTrue(session.authorize(token))

        busy, status = session.acquire(owner="browser-b")
        self.assertEqual(status, 409)
        self.assertFalse(busy["ok"])

        resumed, status = session.acquire(token=token, owner="browser-a")
        self.assertEqual(status, 200)
        self.assertTrue(resumed["ok"])
        self.assertTrue(resumed["resumed"])
        self.assertEqual(resumed["token"], token)

        denied, status = session.release("wrong-token")
        self.assertEqual(status, 403)
        self.assertFalse(denied["ok"])

        released, status = session.release(token)
        self.assertEqual(status, 200)
        self.assertTrue(released["released"])
        self.assertFalse(session.snapshot()["owned"])

    def test_snapshot_reports_requester_ownership(self) -> None:
        session = rp86_public.PublicSession()
        acquired, _ = session.acquire(owner="browser-a")
        token = str(acquired["token"])

        self.assertTrue(session.snapshot(token)["mine"])
        self.assertFalse(session.snapshot("stale-token")["mine"])
        self.assertTrue(session.snapshot("stale-token")["owned"])

    def test_session_has_no_implicit_time_limit(self) -> None:
        session = rp86_public.PublicSession()
        with patch.object(rp86_public.time, "time", return_value=10.0):
            acquired, _ = session.acquire(owner="browser")
        token = str(acquired["token"])
        with patch.object(rp86_public.time, "time", return_value=10_000_000.0):
            self.assertTrue(session.authorize(token))
        self.assertTrue(session.snapshot()["owned"])

    def test_release_is_idempotent_when_available(self) -> None:
        session = rp86_public.PublicSession()
        released, status = session.release(None)
        self.assertEqual(status, 200)
        self.assertTrue(released["ok"])
        self.assertFalse(released["released"])


if __name__ == "__main__":
    unittest.main()
