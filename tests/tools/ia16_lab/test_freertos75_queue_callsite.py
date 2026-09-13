import unittest

from tools.ia16_lab.freertos75_queue_callsite import resolve_consumer_callsite


class FreeRTOS75QueueCallsiteTests(unittest.TestCase):
    def test_resolves_rel16_call_and_argument_setup(self) -> None:
        load = 0x10000
        consumer = 0x10400
        target = 0x10B74
        image = bytearray(b"\x90" * 0x1000)
        setup = consumer + 0x20
        call = setup + 0x0C

        # mov ax,ffff ; push ax ; lea ax,[bp-2] ; push ax ; push [0278]
        sequence = bytearray.fromhex("B8 FF FF 50 8D 46 FE 50 FF 36 78 02")
        displacement = (target - (call + 3)) & 0xFFFF
        sequence += b"\xE8" + displacement.to_bytes(2, "little")
        image[setup - load : setup - load + len(sequence)] = sequence

        result = resolve_consumer_callsite(
            bytes(image),
            load_address=load,
            consumer_address=consumer,
            queue_receive_address=target,
        )
        self.assertEqual(result.setup_address, setup)
        self.assertEqual(result.call_address, call)
        self.assertEqual(result.return_address, call + 3)
        self.assertEqual(result.queue_receive, target)

    def test_rejects_call_to_wrong_target(self) -> None:
        load = 0x10000
        consumer = 0x10400
        image = bytearray(b"\x90" * 0x1000)
        setup = consumer + 0x20
        call = setup + 4
        image[setup - load : setup - load + 4] = bytes.fromhex("B8 FF FF 50")
        displacement = (0x10800 - (call + 3)) & 0xFFFF
        image[call - load : call - load + 3] = b"\xE8" + displacement.to_bytes(2, "little")

        with self.assertRaises(RuntimeError):
            resolve_consumer_callsite(
                bytes(image),
                load_address=load,
                consumer_address=consumer,
                queue_receive_address=0x10B74,
            )


if __name__ == "__main__":
    unittest.main()
