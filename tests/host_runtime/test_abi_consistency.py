"""Cross-language guards for the canonical RP86 hardware and wire ABI."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from rp86_runtime import processor_abi, protocol, runtime_state, workload  # noqa: E402


def _integer(expression: str) -> int:
    normalized = re.sub(r"(?<=\w)[uUlL]+\b", "", expression.strip())
    if not re.fullmatch(r"[0-9a-fA-FxX()\s+*<|&-]+", normalized):
        raise AssertionError(f"unsupported ABI expression: {expression!r}")
    return int(eval(normalized, {"__builtins__": {}}, {}))


def _c_define(text: str, name: str) -> int:
    match = re.search(rf"^#define\s+{re.escape(name)}\s+([^/\r\n]+)", text, re.M)
    if match is None:
        raise AssertionError(f"missing C define {name}")
    return _integer(match.group(1))


def _c_enum(text: str, name: str) -> int:
    match = re.search(rf"\b{re.escape(name)}\s*=\s*([^,\r\n}}]+)", text)
    if match is None:
        raise AssertionError(f"missing C enum {name}")
    return _integer(match.group(1))


def _nasm_define(text: str, name: str) -> int:
    match = re.search(rf"^%define\s+{re.escape(name)}\s+(\S+)", text, re.M)
    if match is None:
        raise AssertionError(f"missing NASM define {name}")
    return _integer(match.group(1))


class AbiConsistencyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.processor_c = (ROOT / "firmware/runtime/processor_abi.h").read_text()
        cls.processor_asm = (ROOT / "processor/include/rp86_abi.inc").read_text()
        cls.protocol_c = (ROOT / "firmware/host_protocol/host_protocol.h").read_text()
        cls.workload_c = (ROOT / "firmware/runtime/workload_protocol.h").read_text()
        cls.sram_c = (ROOT / "firmware/memory/internal_sram_backing.h").read_text()
        cls.mailbox_c = (ROOT / "firmware/memory/shared_mailbox.h").read_text()

    def test_processor_visible_constants_match_c_python_and_nasm(self) -> None:
        names = (
            "INTERRUPT_VECTOR_COMPANION",
            "INTERRUPT_VECTOR_NATIVE_SERVICE",
            "IVT_COMPANION_OFFSET_ADDRESS",
            "IVT_COMPANION_SEGMENT_ADDRESS",
            "IVT_NATIVE_SERVICE_OFFSET_ADDRESS",
            "IVT_NATIVE_SERVICE_SEGMENT_ADDRESS",
            "IO_PORT_PIC_COMMAND",
            "IO_PORT_STATUS",
            "IO_PORT_TX",
            "IO_PORT_RX",
            "IO_PORT_CONTROL",
            "IO_PORT_RESULT",
            "IO_PORT_DIAGNOSTIC",
            "IO_PORT_EXECUTION_CLOCK",
            "CONTROL_IDLE_PREPARE",
            "EXECUTION_CLOCK_REQUEST_FREE_RUNNING",
            "EXECUTION_CLOCK_REQUEST_CLOCK_STEPPED",
        )
        for name in names:
            c_name = f"RP86_{name}"
            expected = getattr(processor_abi, name)
            with self.subTest(name=name):
                self.assertEqual(_c_define(self.processor_c, c_name), expected)
                self.assertEqual(_nasm_define(self.processor_asm, c_name), expected)

    def test_memory_map_matches_c_python_and_nasm(self) -> None:
        sources = {
            "PROCESSOR_ADDRESS_SPACE_SIZE": self.workload_c,
            "INTERNAL_SRAM_PROCESSOR_BASE": self.sram_c,
            "INTERNAL_SRAM_PROCESSOR_SIZE": self.sram_c,
            "SHARED_MAILBOX_BASE": self.mailbox_c,
            "SHARED_MAILBOX_SIZE": self.mailbox_c,
        }
        for name, source in sources.items():
            c_name = f"RP86_{name}"
            expected = getattr(processor_abi, name)
            with self.subTest(name=name):
                self.assertEqual(_c_enum(source, c_name), expected)
                self.assertEqual(_nasm_define(self.processor_asm, c_name), expected)

    def test_host_message_types_and_status_codes_match(self) -> None:
        message_names = (
            "HELLO", "TEXT", "ACK", "COMMAND", "RESULT", "HEARTBEAT",
            "WORKLOAD_BEGIN", "WORKLOAD_DATA", "WORKLOAD_COMMIT",
            "WORKLOAD_CONTROL", "WORKLOAD_STATUS", "WORKLOAD_RESULT",
            "RUNTIME_CONTROL", "RUNTIME_STATUS", "FILESYSTEM_REQUEST",
            "FILESYSTEM_RESULT", "MEMORY_REQUEST", "MEMORY_RESULT", "ERROR",
        )
        for name in message_names:
            self.assertEqual(
                _c_enum(self.protocol_c, f"RP86_HOST_PROTOCOL_MESSAGE_{name}"),
                getattr(protocol, f"TYPE_{name}"),
            )
        status_names = (
            "OK", "BAD_VERSION", "BAD_LENGTH", "BUSY", "TIMEOUT",
            "BAD_SEQUENCE", "SERVICE_UNAVAILABLE", "BAD_CRC", "BAD_STATE",
            "BAD_WORKLOAD", "IO_ERROR", "NOT_FOUND", "INVALID_PATH",
            "NO_SPACE",
        )
        for name in status_names:
            self.assertEqual(
                _c_enum(self.protocol_c, f"RP86_HOST_PROTOCOL_STATUS_{name}"),
                getattr(protocol, f"STATUS_{name}"),
            )

    def test_workload_states_clock_flags_and_reasons_match(self) -> None:
        for value, name in runtime_state.WORKLOAD_STATE_NAMES.items():
            self.assertEqual(
                _c_enum(self.workload_c, f"RP86_WORKLOAD_STATE_{name}"), value
            )
        clock_names = {
            "auto": "AUTO",
            "free-running": "FREE_RUNNING",
            "clock-stepped": "STEPPED",
            "stopped": "STOPPED",
        }
        for python_name, value in workload.CLOCK_MODES.items():
            c_name = clock_names[python_name]
            self.assertEqual(
                _c_enum(self.workload_c, f"RP86_WORKLOAD_CLOCK_{c_name}"), value
            )
        result_flags = {
            "PASS": workload.RESULT_FLAG_PASS,
            "NATIVE_OUTPUT": workload.RESULT_FLAG_NATIVE_OUTPUT,
            "NATIVE_OUTPUT_TRUNCATED": workload.RESULT_FLAG_NATIVE_OUTPUT_TRUNCATED,
            "PROCESSOR_IDENTIFIED": workload.RESULT_FLAG_PROCESSOR_IDENTIFIED,
        }
        for name, value in result_flags.items():
            self.assertEqual(
                _c_enum(self.workload_c, f"RP86_WORKLOAD_RESULT_{name}"), value
            )
        for value, name in workload.COMPLETION_REASONS.items():
            self.assertEqual(
                _c_enum(self.workload_c, f"RP86_WORKLOAD_COMPLETION_{name}"), value
            )

    def test_wire_record_and_structured_payload_are_fixed_size(self) -> None:
        self.assertEqual(_c_define(self.protocol_c, "RP86_HOST_PROTOCOL_MESSAGE_SIZE"), 64)
        self.assertEqual(_c_define(self.protocol_c, "RP86_HOST_PROTOCOL_PAYLOAD_SIZE"), 52)
        self.assertEqual(protocol.MESSAGE_SIZE, 64)
        self.assertEqual(protocol.PAYLOAD_SIZE, 52)
        self.assertEqual(workload._STRUCTURED_STATUS.size, protocol.PAYLOAD_SIZE)
        self.assertEqual(len(protocol.Message(protocol.TYPE_ACK, 1).encode()), 64)


if __name__ == "__main__":
    unittest.main()
