"""Verification of the canonical `host.rp86` package.

Legacy test-module import names are rebound to canonical production modules while
Issue #82 removes the historical `tools/` ownership path. This shim is test-only
and must not become a production compatibility layer.
"""

from __future__ import annotations

import sys

from host import rp86 as _rp86
from host.apps.web import rp86_web as _rp86_web
from scripts import package_workload as _package_workload

sys.modules.setdefault("rp86_runtime", _rp86)
sys.modules.setdefault("rp86_web", _rp86_web)
sys.modules.setdefault("package_workload", _package_workload)
