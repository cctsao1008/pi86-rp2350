import tempfile
import unittest
from pathlib import Path

from host.rp86.workload import WorkloadManifest, encode_workload_file
from tools.ia16_lab import IA16Machine, load_p86w


class IA16MachineTests(unittest.TestCase):
    def test_loads_production_format_and_executes_real_mode_code(self):
        # mov ax,1234h ; mov bx,ax ; nop
        image = bytes.fromhex("B8 34 12 89 C3 90")
        manifest = WorkloadManifest.for_image(
            image,
            load_address=0x10000,
            entry_segment=0x1000,
            entry_offset=0x0000,
            stack_segment=0x2000,
            stack_offset=0xFFF0,
        )

        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "LAB.P86W"
            path.write_bytes(encode_workload_file(manifest, image))
            workload = load_p86w(path)

            machine = IA16Machine()
            machine.load(workload)

            before = machine.registers()
            self.assertEqual((before.cs, before.ip), (0x1000, 0x0000))
            self.assertEqual((before.ss, before.sp), (0x2000, 0xFFF0))
            self.assertEqual(machine.read_memory(0x10000, len(image)), image)

            machine.run(instruction_count=3)
            after = machine.registers()
            self.assertEqual(after.ax, 0x1234)
            self.assertEqual(after.bx, 0x1234)


if __name__ == "__main__":
    unittest.main()
