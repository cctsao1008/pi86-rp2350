"""Interactive terminal rendering and keyboard input."""

from .common import *
from .core import *

def _status_text(
    cpu_sequence: int | None,
    stats: HeartbeatStats,
    connected: bool,
    processor: str = "nec-v30",
    *,
    heartbeat_available: bool = True,
    workload_state: str = "EMPTY",
    clock_mode: str = "AUTO",
    workload_cycles: int = 0,
    processor_state: str = "ACTIVE",
) -> str:
    processor_name = PROCESSOR_NAMES[processor]
    if not heartbeat_available:
        lifecycle = (
            "COMPLETED" if processor_state == "IDLE / HLT" else workload_state
        )
        return (
            f"| ◆ {processor_name}  workload={lifecycle}  "
            f"clock={clock_mode}  cycles={workload_cycles}  "
            f"processor={processor_state}"
        )
    state = "ALIVE" if connected else "LOST"
    latency = f"{stats.last_ms:.1f} ms" if stats.completed else "--"
    sequence = "------" if cpu_sequence is None else f"{cpu_sequence:06d}"
    return (
        f"| {'●' if connected else '○'} {processor_name} {state}  "
        f"cpu_seq={sequence}  rtt={latency}  lost={stats.lost}"
    )


class ConsoleStatus:
    """Own two terminal rows: immutable status above an editable prompt."""

    def __init__(self, processor: str, *, live: bool = True) -> None:
        self._rows = 0
        self._tty = sys.stdout.isatty()
        self._live = live
        self._processor = processor
        self._prompt = self._prompt_for(processor)
        self._heartbeat_available = True
        self._workload_state = "EMPTY"
        self._clock_mode = "AUTO"
        self._workload_cycles = 0
        self._processor_state = "ACTIVE"

    @staticmethod
    def _prompt_for(processor: str) -> str:
        if processor == "intel-8086":
            return "8086"
        if processor == "nec-v30":
            return "V30"
        return "CPU"

    def set_processor(self, processor: str) -> None:
        """Adopt the identity reported by the current physical processor."""
        self._processor = processor
        self._prompt = self._prompt_for(processor)

    def set_runtime(
        self,
        *,
        heartbeat_available: bool,
        workload_state: str,
        clock_mode: str,
        workload_cycles: int,
        processor_state: str,
    ) -> None:
        """Update the single live row from canonical workload telemetry."""
        self._heartbeat_available = heartbeat_available
        self._workload_state = workload_state
        self._clock_mode = clock_mode
        self._workload_cycles = workload_cycles
        self._processor_state = processor_state

    def _erase(self) -> None:
        if self._rows == 0 or not self._tty:
            return
        sys.stdout.write("\r\x1b[2K")
        if self._rows == 2:
            # Move from the prompt row to the status row and clear it too.
            sys.stdout.write("\x1b[1A\r\x1b[2K")

    def render(
        self,
        sequence: int,
        stats: HeartbeatStats,
        connected: bool,
        command_buffer: str,
        cursor: int | None = None,
    ) -> None:
        if not self._tty:
            return
        if not self._live:
            self.render_prompt(command_buffer, cursor)
            return
        self._erase()
        status = _status_text(
            sequence,
            stats,
            connected,
            self._processor,
            heartbeat_available=self._heartbeat_available,
            workload_state=self._workload_state,
            clock_mode=self._clock_mode,
            workload_cycles=self._workload_cycles,
            processor_state=self._processor_state,
        )
        sys.stdout.write(
            f"{status}\n{self._prompt}> {command_buffer}"
        )
        cursor = len(command_buffer) if cursor is None else cursor
        if cursor < len(command_buffer):
            sys.stdout.write(f"\x1b[{len(command_buffer) - cursor}D")
        sys.stdout.flush()
        self._rows = 2

    def render_prompt(self, command_buffer: str, cursor: int | None = None) -> None:
        if not self._tty:
            return
        self._erase()
        sys.stdout.write(f"{self._prompt}> {command_buffer}")
        cursor = len(command_buffer) if cursor is None else cursor
        if cursor < len(command_buffer):
            sys.stdout.write(f"\x1b[{len(command_buffer) - cursor}D")
        sys.stdout.flush()
        self._rows = 1

    def clear(self) -> None:
        self._erase()
        if self._tty:
            sys.stdout.flush()
        self._rows = 0


class CdcDisplayStream:
    """Turn selected CDC evidence lines into concise interactive events."""

    def __init__(self) -> None:
        self._pending = bytearray()
        self._native_group_open = False

    def feed(self, chunk: bytes) -> tuple[str, ...]:
        self._pending.extend(chunk)
        events: list[str] = []
        while b"\n" in self._pending:
            raw, _, remainder = self._pending.partition(b"\n")
            self._pending = bytearray(remainder)
            line = raw.rstrip(b"\r").decode("utf-8", errors="replace")
            if line.startswith("[WORKLOAD START]"):
                self._native_group_open = False
                continue
            if line.startswith("[NATIVE STDOUT]"):
                payload = line.removeprefix("[NATIVE STDOUT]").lstrip()
                if not self._native_group_open:
                    events.append("[NATIVE OUTPUT]")
                    self._native_group_open = True
                events.append(payload)
            elif line.startswith("[WORKLOAD IDLE]"):
                events.append("[WORKLOAD COMPLETED] processor=IDLE / HLT")
                self._native_group_open = False
        return tuple(events)


def _apply_input_character(
    buffer: str, cursor: int, char: str
) -> tuple[str, int, str | None, bool, bool]:
    """Apply one terminal character and report enter/change/Tab state."""
    changed = False
    command: str | None = None
    tab_requested = False
    if char in ("\r", "\n"):
        return "", 0, buffer.strip(), True, False
    if char == "\x03":
        return "", 0, None, True, False
    if char == "\x0c":
        return buffer, cursor, None, True, True
    if char == "\t":
        return buffer, cursor, None, False, False
    if char in ("\b", "\x7f"):
        if cursor:
            return buffer[: cursor - 1] + buffer[cursor:], cursor - 1, None, True, False
        return buffer, cursor, None, False, False
    if char.isprintable():
        return buffer[:cursor] + char + buffer[cursor:], cursor + 1, None, True, False
    return buffer, cursor, command, changed, False


def _read_terminal_command(
    buffer: str, cursor: int,
) -> tuple[str, int, str | None, bool, bool, int, bool]:
    """Read one nonblocking command fragment on Windows or POSIX."""
    if not sys.stdin.isatty():
        return buffer, cursor, None, False, False, 0, False
    if os.name != "nt":
        import select

        changed = False
        tab_requested = False
        history_delta = 0
        clear_requested = False
        command: str | None = None
        while select.select([sys.stdin], [], [], 0)[0]:
            char = sys.stdin.read(1)
            if char == "\x1b" and select.select([sys.stdin], [], [], 0.02)[0]:
                second = sys.stdin.read(1)
                third = ""
                if second in ("[", "O") and select.select([sys.stdin], [], [], 0.02)[0]:
                    third = sys.stdin.read(1)
                if third in ("A", "B"):
                    history_delta = -1 if third == "A" else 1
                    break
                if third == "C":
                    cursor = min(len(buffer), cursor + 1)
                    changed = True
                elif third == "D":
                    cursor = max(0, cursor - 1)
                    changed = True
                elif third in ("H", "F"):
                    cursor = 0 if third == "H" else len(buffer)
                    changed = True
                elif third in ("1", "3", "4", "7", "8"):
                    if select.select([sys.stdin], [], [], 0.02)[0]:
                        sys.stdin.read(1)  # terminal '~' suffix
                    if third == "3" and cursor < len(buffer):
                        buffer = buffer[:cursor] + buffer[cursor + 1 :]
                    elif third in ("1", "7"):
                        cursor = 0
                    elif third in ("4", "8"):
                        cursor = len(buffer)
                    changed = True
                continue
            buffer, cursor, command, one_changed, one_clear = _apply_input_character(
                buffer, cursor, char
            )
            changed = changed or one_changed
            clear_requested = clear_requested or one_clear
            tab_requested = tab_requested or char == "\t"
            if command is not None or tab_requested:
                break
        return buffer, cursor, command, changed, tab_requested, history_delta, clear_requested

    import msvcrt

    changed = False
    tab_requested = False
    history_delta = 0
    clear_requested = False
    command: str | None = None
    while msvcrt.kbhit():
        char = msvcrt.getwch()
        if char in ("\x00", "\xe0"):
            if msvcrt.kbhit():
                key = msvcrt.getwch()
                if key in ("H", "P"):
                    history_delta = -1 if key == "H" else 1
                    break
                if key == "K":
                    cursor = max(0, cursor - 1)
                    changed = True
                elif key == "M":
                    cursor = min(len(buffer), cursor + 1)
                    changed = True
                elif key == "G":
                    cursor = 0
                    changed = True
                elif key == "O":
                    cursor = len(buffer)
                    changed = True
                elif key == "S" and cursor < len(buffer):
                    buffer = buffer[:cursor] + buffer[cursor + 1 :]
                    changed = True
            continue
        buffer, cursor, command, one_changed, one_clear = _apply_input_character(
            buffer, cursor, char
        )
        changed = changed or one_changed
        clear_requested = clear_requested or one_clear
        tab_requested = tab_requested or char == "\t"
        if command is not None or tab_requested:
            break
    return buffer, cursor, command, changed, tab_requested, history_delta, clear_requested
