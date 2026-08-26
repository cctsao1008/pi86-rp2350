"""Interactive terminal rendering and keyboard input."""

from .common import *
from .core import *

def _status_text(
    cpu_sequence: int | None,
    stats: HeartbeatStats,
    connected: bool,
    processor: str = "nec-v30",
) -> str:
    state = "ALIVE" if connected else "LOST"
    latency = f"{stats.last_ms:.1f} ms" if stats.completed else "--"
    sequence = "------" if cpu_sequence is None else f"{cpu_sequence:06d}"
    processor_name = PROCESSOR_NAMES[processor]
    return (
        f"| {'●' if connected else '○'} {processor_name} {state}  "
        f"cpu_seq={sequence}  rtt={latency}  lost={stats.lost}"
    )


class ConsoleStatus:
    """Own two terminal rows: immutable status above an editable prompt."""

    def __init__(self, processor: str) -> None:
        self._rows = 0
        self._tty = sys.stdout.isatty()
        self._processor = processor
        self._prompt = "8086" if processor == "intel-8086" else "V30"

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
        self._erase()
        sys.stdout.write(
            f"{_status_text(sequence, stats, connected, self._processor)}\n"
            f"{self._prompt}> {command_buffer}"
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
