"""Arithmetic oracle for the Intel 8086 100-decimal-digit spigot workload.

The oracle does not execute the NASM binary and cannot establish a
physical-processor PASS. It checks 8086 arithmetic bounds and the exact
independently known decimal prefix.
"""
import unittest

OUTER_LIMIT = 112
ARRAY_LEN = (OUTER_LIMIT * 10) // 3 + 1
OUTPUT_DIGITS = 101
GOLDEN = (
    "3.14159265358979323846264338327950288419716939937510"
    "58209749445923078164062862089986280348253421170679"
)


def spigot_reference():
    data = [2] * ARRAY_LEN
    predigit = 0
    held_nines = 0
    skip_leading_zero = True
    confirmed = []
    max_dividend = max_quotient = 0

    def publish(value):
        nonlocal skip_leading_zero
        assert 0 <= value <= 9
        if skip_leading_zero:
            assert value == 0
            skip_leading_zero = False
            return
        if len(confirmed) < OUTPUT_DIGITS:
            confirmed.append(str(value))

    for iteration in range(OUTER_LIMIT):
        if len(confirmed) == OUTPUT_DIGITS:
            return (
                confirmed[0] + "." + "".join(confirmed[1:]),
                iteration, max_dividend, max_quotient
            )

        carry = 0
        for index in range(ARRAY_LEN, 0, -1):
            dividend = 10 * data[index - 1] + carry * index
            divisor = 2 * index - 1
            carry, data[index - 1] = divmod(dividend, divisor)
            assert 0 <= dividend <= 0xFFFFFFFF, "32-bit MUL/ADC overflow"
            assert 0 <= carry <= 0xFFFF, "Intel 8086 DIV quotient overflow"
            assert 0 < divisor <= 0xFFFF, "Intel 8086 DIV divisor overflow"
            max_dividend = max(max_dividend, dividend)
            max_quotient = max(max_quotient, carry)

        data[0] = carry % 10
        candidate = carry // 10
        if candidate == 9:
            held_nines += 1
        elif candidate == 10:
            publish(predigit + 1)
            for _ in range(held_nines):
                publish(0)
            predigit = 0
            held_nines = 0
        else:
            assert 0 <= candidate <= 8
            publish(predigit)
            for _ in range(held_nines):
                publish(9)
            predigit = candidate
            held_nines = 0

    raise AssertionError("No 101 confirmed digits in 112 iterations")


class Spigot8086ReferenceTests(unittest.TestCase):
    def test_exact_pi_100_fractional_digits(self):
        actual, iterations, _, _ = spigot_reference()
        self.assertEqual(actual, GOLDEN)
        self.assertEqual(len(actual), 102)
        self.assertEqual(iterations, 102)

    def test_bounded_8086_arithmetic(self):
        _, _, max_dividend, max_quotient = spigot_reference()
        self.assertEqual(ARRAY_LEN, 374)
        self.assertEqual(ARRAY_LEN * 2, 748)
        self.assertLessEqual(max_dividend, 0xFFFFFFFF)
        self.assertLessEqual(max_quotient, 0xFFFF)


if __name__ == "__main__":
    unittest.main()
