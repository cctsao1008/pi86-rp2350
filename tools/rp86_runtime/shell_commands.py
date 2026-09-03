"""Command vocabulary for the pi86-rp2350 Host runtime shell.

The shell is intentionally broader than the currently implemented firmware ABI.
Commands remain stable while individual firmware capabilities are brought up.
Unsupported operations must report their missing capability; they must never
pretend that a physical-processor, memory, or storage operation completed.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import shlex


@dataclass(frozen=True)
class CommandSpec:
    name: str
    usage: str
    group: str
    summary: str
    capability: str | None = None
    aliases: tuple[str, ...] = ()


@dataclass(frozen=True)
class ShellCommand:
    spec: CommandSpec
    arguments: tuple[str, ...]
    raw: str


class CommandHistory:
    """Small in-memory history with an editable draft after the newest entry."""

    def __init__(self) -> None:
        self._entries: list[str] = []
        self._cursor = 0
        self._draft = ""

    def remember(self, command: str) -> None:
        command = command.strip()
        if command and (not self._entries or self._entries[-1] != command):
            self._entries.append(command)
        self._cursor = len(self._entries)
        self._draft = ""

    def edit(self, buffer: str) -> None:
        self._cursor = len(self._entries)
        self._draft = buffer

    def move(self, buffer: str, delta: int) -> str:
        if not self._entries or delta == 0:
            return buffer
        if self._cursor == len(self._entries):
            self._draft = buffer
        if delta < 0 and self._cursor > 0:
            self._cursor -= 1
            return self._entries[self._cursor]
        if delta > 0 and self._cursor < len(self._entries) - 1:
            self._cursor += 1
            return self._entries[self._cursor]
        if delta > 0 and self._cursor == len(self._entries) - 1:
            self._cursor = len(self._entries)
            return self._draft
        return buffer


COMMANDS: tuple[CommandSpec, ...] = (
    CommandSpec("help", "help [command]", "shell", "show commands or command help", aliases=("?",)),
    CommandSpec("pwd", "pwd", "shell", "show the current Host directory"),
    CommandSpec("cd", "cd [<Host path>]", "shell", "change the current Host directory"),
    CommandSpec("status", "status", "observe", "show a concise runtime status"),
    CommandSpec("top", "top", "observe", "show the live physical-processor summary"),
    CommandSpec("info", "info", "observe", "show negotiated Host/RP2350 capabilities"),
    CommandSpec("probe", "probe", "observe", "run one prepared-runtime processor identity/liveness diagnostic"),
    CommandSpec("load", "load <Host bin|p86w|flash:/file.p86w> [--address N] [--entry CS:IP] [--stack SS:SP] [--clock MODE]", "workload", "load a native workload", "workload"),
    CommandSpec("run", "run", "workload", "start the loaded workload", "workload"),
    CommandSpec("stop", "stop", "workload", "stop the active workload safely", "workload"),
    CommandSpec("restart", "restart", "workload", "reset and restart the workload", "restart"),
    CommandSpec("bootloader", "bootloader", "control", "stop the processor and enter RP2350 UF2 download mode", "bootloader", aliases=("bootsel",)),
    CommandSpec("send", "send <text>", "console", "send stdin/command text to the processor", "console"),
    CommandSpec("calc", "calc <lhs> <+|-|*|/> <rhs>", "console", "calculate on the physical processor", "console"),
    CommandSpec("console", "console", "console", "enter or describe the interactive console", "console"),
    CommandSpec("stdin", "stdin <file>", "console", "stream a Host file to processor stdin", "console"),
    CommandSpec("stdout", "stdout <file|off>", "console", "capture processor stdout", "console"),
    CommandSpec("ls", "ls [flash:/path|<Host path>]", "files", "list an RP-FLASH or Host directory", "filesystem"),
    CommandSpec("cat", "cat <flash:|sd:/file>", "files", "read a shared file", "filesystem"),
    CommandSpec("put", "put <host-file> <flash:|sd:/path>", "files", "upload an existing Host file; example: put README.md flash:/README.TXT", "filesystem"),
    CommandSpec("get", "get <flash:|sd:/file> [host-file]", "files", "download a shared file", "filesystem"),
    CommandSpec("rm", "rm <flash:|sd:/path>", "files", "remove a shared file", "filesystem"),
    CommandSpec("mv", "mv <source> <destination>", "files", "rename a shared file", "filesystem"),
    CommandSpec("df", "df [flash:|sd:]", "storage", "show storage capacity and availability", "storage"),
    CommandSpec("mount", "mount sd:", "storage", "mount removable SD storage", "sd"),
    CommandSpec("unmount", "unmount sd:", "storage", "flush and unmount SD storage", "sd"),
    CommandSpec("sync", "sync [flash:|sd:]", "storage", "flush persistent filesystem state", "storage"),
    CommandSpec("mem", "mem read|write|load|save ...", "memory", "inspect or change processor-visible memory", "memory"),
    CommandSpec("mailbox", "mailbox <text>", "memory", "exchange one message through shared Internal SRAM", "memory"),
    CommandSpec("trace", "trace [save <host-file>]", "observe", "read/save the stopped executor's last bus cycle and fault flags", "trace"),
    CommandSpec("selftest", "selftest [psram|all]", "observe", "run a canonical runtime resource self-test", "selftest"),
    CommandSpec("timeout", "timeout [seconds|off]", "control", "query/set the RP2350 general-workload execution limit", "watchdog"),
    CommandSpec("regs", "regs", "observe", "show workload-published processor registers", "registers"),
    CommandSpec("quiet", "quiet", "shell", "show errors and command results only"),
    CommandSpec("verbose", "verbose", "shell", "show every native diagnostic probe result"),
    CommandSpec("quit", "quit", "shell", "close the Host shell", aliases=("exit",)),
)


def _command_index() -> dict[str, CommandSpec]:
    result: dict[str, CommandSpec] = {}
    for spec in COMMANDS:
        result[spec.name] = spec
        for alias in spec.aliases:
            result[alias] = spec
    return result


COMMAND_INDEX = _command_index()


def is_device_path(path: str) -> bool:
    """Return whether *path* names an RP2350-owned filesystem."""
    lowered = path.lower()
    return lowered == "flash:" or lowered.startswith("flash:/") or lowered == "sd:" or lowered.startswith("sd:/")


def host_list_path(path: str, base: Path | None = None) -> Path:
    """Resolve a shell ``ls`` argument as a Host path.

    A bare Windows drive designator means its root, not the drive-relative
    working directory used by the Win32 ``C:`` spelling.
    """
    if len(path) == 2 and path[0].isalpha() and path[1] == ":":
        path += os.sep
    result = Path(path).expanduser()
    if base is not None and not result.is_absolute():
        result = base / result
    return result


def format_host_directory(path: str, base: Path | None = None) -> str:
    """Render one non-recursive Host directory listing."""
    directory = host_list_path(path, base)
    if not directory.exists():
        raise ValueError(f"Host path does not exist: {path}")
    if not directory.is_dir():
        raise ValueError(f"Host path is not a directory: {path}")
    try:
        entries = sorted(
            directory.iterdir(), key=lambda item: (not item.is_dir(), item.name.casefold())
        )
    except OSError as exc:
        raise ValueError(f"cannot list Host directory {path}: {exc}") from exc
    lines = []
    for entry in entries:
        try:
            kind = "<DIR>" if entry.is_dir() else f"{entry.stat().st_size:>10}"
        except OSError:
            kind = "<ERR>"
        lines.append(f"{kind:>10}  {entry.name}")
    return f"Directory of Host {directory}\n" + ("\n".join(lines) if lines else "<empty>")


def _replace_last_token(line: str, replacement: str) -> str:
    split_at = max(line.rfind(" "), line.rfind("\t")) + 1
    return line[:split_at] + replacement


def completion_token(line: str) -> str:
    """Return the unquoted final token used by the lightweight line editor."""
    split_at = max(line.rfind(" "), line.rfind("\t")) + 1
    return line[split_at:].strip('"')


def complete_shell_input(
    line: str,
    remote_entries: tuple[tuple[str, bool], ...] = (),
    host_base: Path | None = None,
) -> tuple[str, tuple[str, ...]]:
    """Complete commands, Host paths, or caller-supplied device entries.

    The return value is ``(new_line, candidates)``.  One match is inserted;
    multiple matches are displayed by the caller without disturbing the live
    runtime lifecycle status line.
    """
    token = completion_token(line)
    if not line.strip() or (" " not in line and "\t" not in line):
        prefix = line.strip().casefold()
        names = tuple(
            spec.name for spec in COMMANDS if spec.name.casefold().startswith(prefix)
        )
        if len(names) == 1:
            return names[0] + " ", names
        return line, names

    if token.lower().startswith(("flash:", "sd:")):
        slash = token.rfind("/")
        base = token[: slash + 1] if slash >= 0 else token + "/"
        prefix = token[slash + 1 :] if slash >= 0 else ""
        matches = tuple(
            base + name + ("/" if directory else "")
            for name, directory in remote_entries
            if name.casefold().startswith(prefix.casefold())
        )
    else:
        expanded = os.path.expanduser(token or ".")
        candidate_path = host_list_path(expanded, host_base)
        if token.endswith(("/", "\\")):
            parent, prefix = candidate_path, ""
            base = token
        elif len(token) == 2 and token[0].isalpha() and token[1] == ":":
            parent, prefix = candidate_path, ""
            base = token + os.sep
        else:
            parent, prefix = candidate_path.parent, candidate_path.name
            base = token[: len(token) - len(prefix)] if prefix else token
        if str(parent) == "":
            parent = Path(".")
        try:
            entries = sorted(parent.iterdir(), key=lambda item: item.name.casefold())
        except OSError:
            entries = []
        separator = "\\" if "\\" in base else "/" if "/" in base else os.sep
        matches = tuple(
            base + entry.name + (separator if entry.is_dir() else "")
            for entry in entries
            if entry.name.casefold().startswith(prefix.casefold())
        )

    if len(matches) == 1:
        replacement = matches[0]
        if " " in replacement and not replacement.startswith('"'):
            replacement = f'"{replacement}"'
        return _replace_last_token(line, replacement), matches
    return line, matches


def parse_command(line: str) -> ShellCommand | None:
    """Parse one shell line without executing or claiming a device operation."""
    try:
        lexer = shlex.shlex(line, posix=True)
        lexer.whitespace_split = True
        lexer.commenters = ""
        # A Windows Host path uses backslashes as separators, not escapes.
        lexer.escape = ""
        words = list(lexer)
    except ValueError as exc:
        raise ValueError(f"command syntax error: {exc}") from exc
    if not words:
        return None
    spec = COMMAND_INDEX.get(words[0].lower())
    if spec is None:
        raise ValueError(f"unknown command: {words[0]!r}; type help")
    return ShellCommand(spec, tuple(words[1:]), line)


def command_help(name: str | None = None) -> str:
    """Render stable, grouped command help for humans and documentation tests."""
    if name:
        spec = COMMAND_INDEX.get(name.lower())
        if spec is None:
            raise ValueError(f"unknown command: {name!r}")
        availability = (
            "available when capability is negotiated: " + spec.capability
            if spec.capability
            else "Host shell command"
        )
        return f"{spec.usage}\n  {spec.summary}\n  {availability}"

    lines = ["Commands:"]
    groups: list[str] = []
    for spec in COMMANDS:
        if spec.group not in groups:
            groups.append(spec.group)
    for group in groups:
        lines.append(f"  {group}:")
        for spec in COMMANDS:
            if spec.group == group:
                lines.append(f"    {spec.usage:<44} {spec.summary}")
    return "\n".join(lines)


def unavailable_message(command: ShellCommand) -> str:
    """Explain an unavailable backend without presenting a false success."""
    capability = command.spec.capability or command.spec.name
    return (
        f"{command.spec.name}: NOT AVAILABLE "
        f"(RP2350 capability {capability!r} was not negotiated)"
    )
