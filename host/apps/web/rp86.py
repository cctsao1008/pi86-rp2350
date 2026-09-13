#!/usr/bin/env python3
"""Internal Web-app launcher for the canonical RP86 Host CLI."""

from __future__ import annotations

from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[3]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

runpy.run_module("host.apps.cli.rp86", run_name="__main__")
