#!/usr/bin/env python3
"""Canonical RP86 Host runtime entry point."""

from __future__ import annotations

import sys


if "--public" in sys.argv or "--remote" in sys.argv:
    if "--public" in sys.argv:
        sys.argv.remove("--public")
    if "--remote" in sys.argv:
        sys.argv.remove("--remote")
    from host.apps.remote.rp86_remote import main
elif "--web" in sys.argv:
    sys.argv.remove("--web")
    from host.apps.web.rp86_web import main
elif "--start-workload" in sys.argv:
    from host.rp86.start_workload import main
else:
    from host.rp86.cli import main


raise SystemExit(main())
