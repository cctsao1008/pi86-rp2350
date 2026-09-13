"""Remote-facing RP86 Host application entry points.

The remote gateway reuses the canonical Web API and Host runtime. Historical
absolute module names are rebound here while the repository migration removes
the old `tools/` layout.
"""

from __future__ import annotations

import sys

from host import rp86 as _rp86
from host.apps.web import rp86_web_api as _rp86_web_api

sys.modules.setdefault("rp86_runtime", _rp86)
sys.modules.setdefault("rp86_web_api", _rp86_web_api)
