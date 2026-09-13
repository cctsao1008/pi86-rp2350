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
            stack_offset=0xFFF0,
        )
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "LAB.P86W"
        path.write_bytes(encode_workload_file(manifest, image))
        machine = IA16Machine()
        machine.load(load_p86w(path))
        return machine

    def test_loads_production_format_and_executes_real_mode_code(self):
        machine = self._machine(bytes.fromhex("B8 34 12 89 C3 90"))
        before = machine.registers()
        self.assertEqual((before.cs, before.ip), (0x1000, 0x0000))
        self.assertEqual((before.ss, before.sp), (0x2000, 0xFFF0))

        machine.run(instruction_count=3)
        after = machine.registers()
        self.assertEqual(after.ax, 0x1234)
        self.assertEqual(after.bx, 0x1234)

    def test_fixture_can_set_explicit_ia16_register_state(self):
        machine = self._machine(b"\x90")
        machine.write_register("ds", 0x1279)
        machine.write_register("ss", 0x1279)
        machine.write_register("sp", 0xF000)
        machine.write_register("cs", 0x1000)
        machine.write_register("ip", 0x2613)

        state = machine.registers()
        self.assertEqual((state.ds, state.ss, state.sp), (0x1279, 0x1279, 0xF000))
        self.assertEqual((state.cs, state.ip), (0x1000, 0x2613))

        with self.assertRaises(ValueError):
            machine.write_register("not-a-register", 0)
        with self.assertRaises(ValueError):
            machine.write_register("ax", 0x10000)

    def test_run_until_stops_before_target_instruction(self):
        # mov ax,1234h ; mov bx,ax ; nop
        machine = self._machine(bytes.fromhex("B8 34 12 89 C3 90"))
        self.assertTrue(machine.run_until_address(0x10003, instruction_count=8))
        state = machine.registers()
        self.assertEqual(machine.current_linear_ip(), 0x10003)
        self.assertEqual(state.ax, 0x1234)
        self.assertEqual(state.bx, 0x0000)

        machine.run(instruction_count=1)
        self.assertEqual(machine.registers().bx, 0x1234)

    def test_run_until_reports_unreached_target(self):
        machine = self._machine(b"\x90\x90\x90")
        self.assertFalse(machine.run_until_address(0x10002, instruction_count=1))
        self.assertEqual(machine.current_linear_ip(), 0x10001)


if __name__ == "__main__":
    unittest.main()
