#!/usr/bin/env python3
"""Pre-Codex host bridge and deterministic protocol simulator.

The simulator is intentionally transport-only.  HID support is added after
the physical AI-B0 gate passes, without changing the Message ABI.
"""

from __future__ import annotations

import argparse

from protocol import Message, TYPE_HELLO, TYPE_TEXT

CANONICAL_GREETING = b"HELLO NEC V30"
CANONICAL_REPLY = b"HELLO OPENAI CODEX"


def simulate_v30(record: bytes) -> bytes:
    request = Message.decode(record)
    if request.message_type != TYPE_HELLO or request.payload != CANONICAL_GREETING:
        raise ValueError("simulated V30 rejected the greeting")
    return Message(TYPE_TEXT, request.sequence, CANONICAL_REPLY).encode()


def main() -> int:
    parser = argparse.ArgumentParser(description="pi86-rp2350 host bridge")
    parser.add_argument(
        "--simulate",
        action="store_true",
        help="run the canonical exchange without USB hardware",
    )
    parser.add_argument("--sequence", type=int, default=1)
    args = parser.parse_args()

    if not args.simulate:
        parser.error("only --simulate is available before the HID gate")

    request = Message(TYPE_HELLO, args.sequence, CANONICAL_GREETING)
    response = Message.decode(simulate_v30(request.encode()))
    print(f"OpenAI Codex > {request.payload.decode('ascii')}")
    print(f"NEC V30      > {response.payload.decode('ascii')}")
    print("Protocol simulation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
