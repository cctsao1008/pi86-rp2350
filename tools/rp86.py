#!/usr/bin/env python3
"""Canonical RP86 Host runtime entry point."""

import sys


if "--public" in sys.argv:
    sys.argv.remove("--public")
    from rp86_public import main
elif "--web" in sys.argv:
    sys.argv.remove("--web")
    from rp86_web import main
elif "--start-workload" in sys.argv:
    from rp86_runtime.start_workload import main
else:
    from rp86_runtime.cli import main


raise SystemExit(main())
