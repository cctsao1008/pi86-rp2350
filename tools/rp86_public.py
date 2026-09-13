#!/usr/bin/env python3
"""Compatibility launcher for the canonical RP86 remote gateway."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from host.apps.remote.rp86_remote import main


if __name__ == "__main__":
    raise SystemExit(main())
