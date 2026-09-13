"""Native 8086-class calculator command ABI.

The Host parses presentation syntax only.  It sends the operator and two
unsigned 16-bit operands to the physical processor; the native ISR executes
the selected arithmetic instruction and returns the observed result.
"""

from __future__ import annotations

import re
import struct


CALCULATOR_MAGIC = 0xCA1C
CALCULATOR_OPERATIONS = {"+": 1, "-": 2, "*": 3, "/": 4}
_CALCULATOR = struct.Struct("<7H")
_EXPRESSION = re.compile(
    r"^\s*(0[xX][0-9a-fA-F]+|\d+)\s*([+\-*/])\s*"
    r"(0[xX][0-9a-fA-F]+|\d+)\s*$"
)


def calculator_payload(arguments: tuple[str, ...]) -> bytes:
    """Encode ``calc <lhs><op><rhs>`` without evaluating the expression."""
    expression = " ".join(arguments)
    match = _EXPRESSION.fullmatch(expression)
    if match is None:
        raise ValueError("usage: calc <0..65535> <+|-|*|/> <0..65535>")
    lhs = int(match.group(1), 0)
    rhs = int(match.group(3), 0)
    if not 0 <= lhs <= 0xFFFF or not 0 <= rhs <= 0xFFFF:
        raise ValueError("calculator operands must be unsigned 16-bit values")
    operation = CALCULATOR_OPERATIONS[match.group(2)]
    if operation == CALCULATOR_OPERATIONS["/"] and rhs == 0:
        raise ValueError("division by zero is not sent to the physical processor")
    return _CALCULATOR.pack(CALCULATOR_MAGIC, operation, lhs, rhs, 0, 0, 0)


def is_calculator_payload(payload: bytes) -> bool:
    """Return whether a command carries the fixed seven-word calculator ABI."""
    return len(payload) == _CALCULATOR.size and struct.unpack_from("<H", payload)[0] == CALCULATOR_MAGIC

