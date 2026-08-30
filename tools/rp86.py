#!/usr/bin/env python3
"""Canonical RP86 Host runtime entry point."""

import sys


if "--web" in sys.argv:
    sys.argv.remove("--web")
    from rp86_web import main
else:
    from rp86_runtime.cli import main


raise SystemExit(main())
