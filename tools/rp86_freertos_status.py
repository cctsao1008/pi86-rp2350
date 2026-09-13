#!/usr/bin/env python3
"""Compatibility launcher for the canonical FreeRTOS diagnostic tool."""

from tools.diagnostics.freertos_status import main


if __name__ == "__main__":
    raise SystemExit(main())
