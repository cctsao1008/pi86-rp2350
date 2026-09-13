import tempfile
import unittest
from pathlib import Path

from host.rp86.workload import WorkloadManifest, encode_workload_file
from tools.ia16_lab import IA16Machine, load_p86w


class IA16MachineTests(unittest.TestCase):
    def _machine(self, image: bytes) -> IA16Machine:
        manifest = WorkloadManifest.for_image(
            image,
            load_address=0x10000,
            entry_segment=0x1000,
            entry_offset=0x0000,
            stack_segment=0x2000,
            stack_offset=0x0FF0,
        )
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "LAB.P86W"
        path.write_bytes(encode_workload_file(manifest, image))
        machine = IA16Machine()
        machine.load(load_p86w(path))
        return machine

    def test_loads_production_format_and_executes_real_mode_code(self):
        # mov ax,1234h ; mov bx,ax ; nop
        image = bytes.fromhex("B8 34 12 89 C3 90")
        machine = self._machine(image)

        before = machine.registers()
        self.assertEqual((before.cs, before.ip), (0x1000, 0x0000))
        self.assertEqual((before.ss, before.sp), (0x2000, 0x0FF0))
        self.assertEqual(machine.read_memory(0x10000, len(image)), image)

        machine.run(instruction_count=3)
        after = machine.registers()
        self.assertEqual(after.ax, 0x1234)
        self.assertEqual(after.bx, 0x1234)

    def test_fixture_can_set_explicit_ia16_register_state(self):
        machine = self._machine(b"\x90")

        machine.write_register("ds", 0x1279)
        machine.write_register("ss", 0x1279)
        machine.write_register("sp", 0x0F00)
        machine.write_register("cs", 0x1000)
        machine.write_register("ip", 0x2613)

        state = machine.registers()
        self.assertEqual((state.ds, state.ss, state.sp), (0x1279, 0x1279, 0x0F00))
        self.assertEqual((state.cs, state.ip), (0x1000, 0x2613))

        with self.assertRaises(ValueError):
            machine.write_register("not-a-register", 0)
        with self.assertRaises(ValueError):
            machine.write_register("ax", 0x10000)

    def test_observable_out_hook_does_not_model_device_behavior(self):
        # mov dx,1234h ; mov ax,5678h ; out dx,ax ; nop
        machine = self._machine(bytes.fromhex("BA 34 12 B8 78 56 EF 90"))
        writes = []
        machine.install_io_handlers(
            on_out=lambda port, size, value: writes.append((port, size, value))
        )

        machine.run(instruction_count=4)
        self.assertEqual(writes, [(0x1234, 2, 0x5678)])

        with self.assertRaises(RuntimeError):
            machine.install_io_handlers(on_out=lambda _port, _size, _value: None)

    def test_inject_real_mode_interrupt_builds_8086_frame_and_iret_returns(self):
        # Main code is just NOPs. The handler at 1000:0020 is IRET.
        image = bytearray(b"\x90" * 0x40)
        image[0x20] = 0xCF
        machine = self._machine(bytes(image))

        vector = 0x21
        machine.write_memory(vector * 4, bytes.fromhex("20 00 00 10"))
        machine.write_register("cs", 0x1000)
        machine.write_register("ip", 0x0003)
        machine.write_register("ss", 0x2000)
        machine.write_register("sp", 0x0F00)
        machine.write_register("flags", 0x0302)  # IF=1, TF=1.

        before = machine.registers()
        machine.inject_real_mode_interrupt(vector)
        entered = machine.registers()

        self.assertEqual((entered.cs, entered.ip), (0x1000, 0x0020))
        self.assertEqual(entered.sp, 0x0EFA)
        self.assertEqual(entered.flags & 0x0300, 0)
        frame = machine.read_memory((0x2000 << 4) + entered.sp, 6)
        self.assertEqual(
            frame,
            bytes.fromhex("03 00 00 10 02 03"),
        )

        machine.run(instruction_count=1)
        after = machine.registers()
        self.assertEqual((after.cs, after.ip), (before.cs, before.ip))
        self.assertEqual((after.ss, after.sp), (before.ss, before.sp))
        self.assertEqual(after.flags & 0xFFFF, before.flags & 0xFFFF)

    def test_inject_real_mode_interrupt_rejects_invalid_vector(self):
        machine = self._machine(b"\x90")
        with self.assertRaises(ValueError):
            machine.inject_real_mode_interrupt(-1)
        with self.assertRaises(ValueError):
            machine.inject_real_mode_interrupt(0x100)


if __name__ == "__main__":
    unittest.main()
