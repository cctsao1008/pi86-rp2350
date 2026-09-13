"""Local Web console for RP86 Host software.

The moved Web modules still use their historical absolute module names internally.
During the repository migration, bind those names to the canonical Host packages
so the application no longer depends on the old `tools/` location.
"""

from __future__ import annotations

import sys

from host import rp86 as _rp86

sys.modules.setdefault("rp86_runtime", _rp86)

from . import rp86_web_view as _rp86_web_view  # noqa: E402

sys.modules.setdefault("rp86_web_view", _rp86_web_view)

from . import rp86_web_api as _rp86_web_api  # noqa: E402

sys.modules.setdefault("rp86_web_api", _rp86_web_api)
