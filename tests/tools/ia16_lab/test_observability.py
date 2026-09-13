import tempfile
import unittest
from pathlib import Path

from host.rp86.workload import WorkloadManifest, encode_workload_file
from tools.ia16_lab import (
    IA16Machine,
    InstructionTrace,
    MemoryTrace,
    SymbolTable,
    TraceRecorder,
    WatchRange,
    load_p86w,
)


class IA16ObservabilityTests(unittest.TestCase):
    def _machine(self) -> IA16Machine:
        # mov ax,1234h ; mov [0010h],ax ; mov bx,[0010h] ; nop
        image = bytes.fromhex("B8 34 12 A3 10 00 8B 1E 10 00 90")
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
        path = Path(temp.name) / "TRACE.P86W"
        path.write_bytes(encode_workload_file(manifest, image))
        machine = IA16Machine()
        machine.load(load_p86w(path))
        return machine

    def test_instruction_and_memory_events_capture_register_state(self) -> None:
        machine = self._machine()
        recorder = TraceRecorder()
        machine.install_trace(recorder)
        machine.run(instruction_count=4)

        instructions = [event for event in recorder.events if isinstance(event, InstructionTrace)]
        memory = [event for event in recorder.events if isinstance(event, MemoryTrace)]
        self.assertEqual(len(instructions), 4)
        self.assertEqual([event.kind for event in memory], ["WRITE", "READ"])
        self.assertEqual(memory[0].address, 0x0010)
        self.assertEqual(memory[0].value & 0xFFFF, 0x1234)
        self.assertEqual(memory[1].value, 0x1234)
        self.assertEqual(machine.registers().bx, 0x1234)

    def test_watch_ranges_filter_trace_volume(self) -> None:
        machine = self._machine()
        recorder = TraceRecorder(
            watches=(WatchRange(0x0010, 0x0012, "data"),),
            max_events=8,
        )
        machine.install_trace(recorder)
        machine.run(instruction_count=4)

        self.assertTrue(recorder.events)
        self.assertTrue(all(isinstance(event, MemoryTrace) for event in recorder.events))
        self.assertEqual({event.address for event in recorder.events}, {0x0010})

    def test_wlink_symbols_annotate_addresses(self) -> None:
        symbols = SymbolTable.from_wlink_map(
            "  1000:0000       _entry\n"
            "  1000:0006+      _load_value\n"
            "  2000:0010       _object\n"
        )
        self.assertEqual(symbols.address("_entry"), 0x10000)
        self.assertEqual(symbols.resolve(0x10008), "_load_value+0x2")
        self.assertEqual(symbols.span("_entry"), (0x10000, 0x10006))

    def test_segment_candidates_are_evidence_not_claimed_decoding(self) -> None:
        machine = self._machine()
        recorder = TraceRecorder(watches=(WatchRange(0x0010, 0x0012),))
        machine.install_trace(recorder)
        machine.run(instruction_count=4)
        event = next(event for event in recorder.events if isinstance(event, MemoryTrace))
        self.assertIn("DS:0010", event.segment_candidates)


if __name__ == "__main__":
    unittest.main()
