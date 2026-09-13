"""Reference model for the Issue #106 dynamic SRAM bus experiment.

This is host-side engineering tooling only.  It mirrors the locked Pi86 HAT
GPIO permutation so candidate PIO/DMA implementations can be checked against a
simple, auditable model before physical timing work begins.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum


AD_GPIO = (
    26, 19, 13, 6, 15, 0, 10, 12,
    11, 22, 27, 17, 14, 3, 2, 4,
)
A16_A19_GPIO = (5, 18, 23, 24)
UBE_GPIO = 25

PROCESSOR_ADDRESS_MASK = 0xFFFFF
INTERNAL_SRAM_BASE = 0x2000_0000


class Lane(Enum):
    LOW_BYTE = "low-byte"
    HIGH_BYTE = "high-byte"
    WORD = "word"
    INVALID = "invalid"


def _bit(value: int, index: int) -> int:
    return (value >> index) & 1


def encode_address_gpio(address: int, *, ube_asserted: bool | None = None) -> int:
    """Encode an 8086/V30 physical address into the HAT's raw GPIO bitmap.

    AD0 carries A0 during T1.  A16..A19 use their dedicated scattered GPIOs.
    UBE is optional because it describes lane selection rather than the linear
    physical address itself.
    """
    if not 0 <= address <= PROCESSOR_ADDRESS_MASK:
        raise ValueError("processor address must be 20-bit")

    raw = 0
    for address_bit, gpio in enumerate(AD_GPIO):
        raw |= _bit(address, address_bit) << gpio
    for high_bit, gpio in enumerate(A16_A19_GPIO, start=16):
        raw |= _bit(address, high_bit) << gpio

    if ube_asserted is not None:
        # UBE is active low on the current HAT.
        if not ube_asserted:
            raw |= 1 << UBE_GPIO
    return raw


def decode_address_gpio(raw_gpio: int) -> int:
    """Decode the scattered HAT address pins into a contiguous 20-bit address."""
    address = 0
    for address_bit, gpio in enumerate(AD_GPIO):
        address |= _bit(raw_gpio, gpio) << address_bit
    for high_bit, gpio in enumerate(A16_A19_GPIO, start=16):
        address |= _bit(raw_gpio, gpio) << high_bit
    return address & PROCESSOR_ADDRESS_MASK


def encode_data_gpio(value: int) -> int:
    """Encode a 16-bit data word into the scattered AD GPIO output bitmap."""
    if not 0 <= value <= 0xFFFF:
        raise ValueError("data must be 16-bit")
    raw = 0
    for data_bit, gpio in enumerate(AD_GPIO):
        raw |= _bit(value, data_bit) << gpio
    return raw


def decode_data_gpio(raw_gpio: int) -> int:
    """Decode the scattered AD GPIO input bitmap into a 16-bit data word."""
    value = 0
    for data_bit, gpio in enumerate(AD_GPIO):
        value |= _bit(raw_gpio, gpio) << data_bit
    return value


def classify_lane(raw_gpio: int) -> Lane:
    """Classify the 8086 byte lane from T1 A0 and active-low UBE/BHE."""
    a0 = _bit(raw_gpio, AD_GPIO[0])
    ube_asserted = _bit(raw_gpio, UBE_GPIO) == 0

    if a0 == 0 and ube_asserted:
        return Lane.WORD
    if a0 == 0 and not ube_asserted:
        return Lane.LOW_BYTE
    if a0 == 1 and ube_asserted:
        return Lane.HIGH_BYTE
    return Lane.INVALID


def sram_pointer(processor_address: int, *, backing_offset: int = 0) -> int:
    """Return the full RP2350 SRAM pointer for a 1:1 processor backing window."""
    if not 0 <= processor_address <= PROCESSOR_ADDRESS_MASK:
        raise ValueError("processor address must be 20-bit")
    if backing_offset < 0:
        raise ValueError("backing_offset must be non-negative")
    return INTERNAL_SRAM_BASE + backing_offset + processor_address


@dataclass(frozen=True)
class Transaction:
    address: int
    lane: Lane
    data: int | None = None


def decode_t1(raw_gpio: int) -> Transaction:
    return Transaction(
        address=decode_address_gpio(raw_gpio),
        lane=classify_lane(raw_gpio),
    )
