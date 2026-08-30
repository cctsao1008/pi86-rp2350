#!/usr/bin/env python3
"""Canonical RP86 Host runtime entry point."""

import os
import sys


_WEB_OWNER_ENV = "RP86_WEB_OWNER"


def _rewrite_web_owned_runtime_args() -> None:
    """Use the proven exchange+heartbeat path for Web-owned background sessions.

    rp86_web.py currently launches a quiet background child with
    --interactive --heartbeat --attach.  When that child is descended from the
    Web entry point, replace the attach-only startup with the canonical
    physical exchange path so RESET/runtime acceptance and native processor
    identity are established before persistent heartbeat monitoring begins.

    The environment marker is set only by the --web parent, so normal manual
    CLI --attach semantics remain unchanged.
    """
    if os.environ.get(_WEB_OWNER_ENV) != "1":
        return
    required = {"--interactive", "--heartbeat", "--attach"}
    if not required.issubset(sys.argv):
        return
    sys.argv[:] = [
        arg
        for arg in sys.argv
        if arg not in {"--interactive", "--attach"}
    ]
    if "--exchange" not in sys.argv:
        sys.argv.insert(1, "--exchange")


if "--web" in sys.argv:
    sys.argv.remove("--web")
    os.environ[_WEB_OWNER_ENV] = "1"
    from rp86_web import main
else:
    _rewrite_web_owned_runtime_args()
    from rp86_runtime.cli import main


raise SystemExit(main())
