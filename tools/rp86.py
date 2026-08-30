#!/usr/bin/env python3
"""Canonical RP86 Host runtime entry point."""

import os
import sys


_WEB_OWNER_ENV = "RP86_WEB_OWNER"
_WEB_OWNER_STARTUP_WAIT_S = 75.0


def _rewrite_web_owned_runtime_args() -> None:
    """Use the proven exchange+heartbeat path for Web-owned background sessions.

    rp86_web.py launches a quiet background child with
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


def _web_main() -> int:
    """Run the Web console after its Web-owned processor runtime is ready.

    A fresh --exchange path performs the full physical acceptance sequence
    before persistent_monitor() creates the Host Broker.  That takes longer
    than the Web console's original four-second startup window.  Keep the HTTP
    server offline during this acceptance phase so auto-refresh cannot open a
    competing CDC status transaction while the background owner is bringing
    up the real processor.
    """
    os.environ[_WEB_OWNER_ENV] = "1"
    import rp86_web

    original_ensure_runtime_owner = rp86_web._ensure_runtime_owner

    def ensure_runtime_owner(wait_seconds: float = _WEB_OWNER_STARTUP_WAIT_S):
        return original_ensure_runtime_owner(wait_seconds=wait_seconds)

    rp86_web._ensure_runtime_owner = ensure_runtime_owner
    return rp86_web.main()


if "--web" in sys.argv:
    sys.argv.remove("--web")
    main = _web_main
else:
    _rewrite_web_owned_runtime_args()
    from rp86_runtime.cli import main


raise SystemExit(main())
