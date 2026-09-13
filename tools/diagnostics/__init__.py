"""Developer-facing diagnostic tools.

During the repository restructure, historical `rp86_runtime` imports are rebound
to the canonical Host package so diagnostics do not depend on legacy paths.
"""

from __future__ import annotations

import sys

from host import rp86 as _rp86

sys.modules.setdefault("rp86_runtime", _rp86)
