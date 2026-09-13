import random
import unittest

from tools.diagnostics.issue106_sram_bus_model import (
    Lane,
    classify_lane,
    decode_address_gpio,
    decode_data_gpio,
    encode_address_gpio,
    encode_data_gpio,
    sram_pointer,
)


class Issue106SramBusModelTests(unittest.TestCase):
    def test_address_round_trip_full_20_bit_space_samples(self) -> None:
        vectors = [
            0x00000,
            0x00001,
            0x00002,
            0x0FFFF,
            0x10000,
            0x7FFFF,
            0x80000,
            0xFFFFE,
            0xFFFFF,
        ]
        rng = random.Random(106)
        vectors.extend(rng.randrange(0x100000) for _ in range(4096))

        for address in vectors:
            with self.subTest(address=address):
                raw = encode_address_gpio(address)
                self.assertEqual(decode_address_gpio(raw), address)

    def test_data_round_trip(self) -> None:
        vectors = [0x0000, 0x0001, 0x00FF, 0x5500, 0xAA55, 0xFFFF]
        rng = random.Random(2350)
        vectors.extend(rng.randrange(0x10000) for _ in range(4096))

        for value in vectors:
            with self.subTest(value=value):
                self.assertEqual(decode_data_gpio(encode_data_gpio(value)), value)

    def test_lane_classification(self) -> None:
        low = encode_address_gpio(0x12340, ube_asserted=False)
        word = encode_address_gpio(0x12340, ube_asserted=True)
        high = encode_address_gpio(0x12341, ube_asserted=True)
        invalid = encode_address_gpio(0x12341, ube_asserted=False)

        self.assertEqual(classify_lane(low), Lane.LOW_BYTE)
        self.assertEqual(classify_lane(word), Lane.WORD)
        self.assertEqual(classify_lane(high), Lane.HIGH_BYTE)
        self.assertEqual(classify_lane(invalid), Lane.INVALID)

    def test_sram_pointer_is_full_dma_address(self) -> None:
        self.assertEqual(sram_pointer(0x01234), 0x20001234)
        self.assertEqual(
            sram_pointer(0x01234, backing_offset=0x10000),
            0x20011234,
        )

    def test_invalid_address_rejected(self) -> None:
        with self.assertRaises(ValueError):
            encode_address_gpio(-1)
        with self.assertRaises(ValueError):
            encode_address_gpio(0x100000)
        with self.assertRaises(ValueError):
            sram_pointer(0x100000)


if __name__ == "__main__":
    unittest.main()
