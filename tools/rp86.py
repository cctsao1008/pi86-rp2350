#!/usr/bin/env python3
"""Compatibility launcher for the canonical Host CLI.

The implementation moved to `host/apps/cli/rp86.py` under Issue #82.
"""

from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

runpy.run_module("host.apps.cli.rp86", run_name="__main__")
