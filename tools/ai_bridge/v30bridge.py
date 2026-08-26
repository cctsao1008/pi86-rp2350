#!/usr/bin/env python3
"""Compatibility entry point for the former v30bridge command name.

New Host code should import :mod:`rp86_runtime` or run ``tools/rp86.py``.
"""

from __future__ import annotations

from pathlib import Path
import sys

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from rp86_runtime.cli import *  # noqa: F401,F403,E402
from rp86_runtime.console import _status_text  # noqa: F401,E402
from rp86_runtime.core import *  # noqa: F401,F403,E402
from rp86_runtime.transport import *  # noqa: F401,F403,E402
from rp86_runtime.transport import _open_hid  # noqa: F401,E402


if __name__ == "__main__":
    raise SystemExit(main())
