#!/usr/bin/env python3
"""Compatibility launcher for the canonical RP86 Web app."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from host.apps.web.rp86_web import main


if __name__ == "__main__":
    raise SystemExit(main())
