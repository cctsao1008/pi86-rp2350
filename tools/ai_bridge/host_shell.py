"""Command vocabulary for the pi86-rp2350 Host runtime shell.

The shell is intentionally broader than the currently implemented firmware ABI.
Commands remain stable while individual firmware capabilities are brought up.
Unsupported operations must report their missing capability; they must never
pretend that a V30, memory, or storage operation completed.
"""

from __future__ import annotations

from dataclasses import dataclass
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


COMMANDS: tuple[CommandSpec, ...] = (
    CommandSpec("help", "help [command]", "shell", "show commands or command help", aliases=("?",)),
    CommandSpec("status", "status", "observe", "show a concise runtime status"),
    CommandSpec("top", "top", "observe", "show the live V30 machine summary"),
    CommandSpec("info", "info", "observe", "show negotiated Host/RP2350 capabilities"),
    CommandSpec("ping", "ping", "control", "request one physical V30 liveness proof"),
    CommandSpec("load", "load <bin> [--address N] [--entry CS:IP]", "workload", "load a native workload", "workload"),
    CommandSpec("run", "run", "workload", "start the loaded workload", "workload"),
    CommandSpec("stop", "stop", "workload", "stop the active workload safely", "workload"),
    CommandSpec("restart", "restart", "workload", "reset and restart the workload", "restart"),
    CommandSpec("bootloader", "bootloader", "control", "stop the V30 and enter RP2350 UF2 download mode", "bootloader", aliases=("bootsel",)),
    CommandSpec("send", "send <text>", "console", "send stdin/command text to the V30", "console"),
    CommandSpec("console", "console", "console", "enter or describe the interactive console", "console"),
    CommandSpec("stdin", "stdin <file>", "console", "stream a Host file to V30 stdin", "console"),
    CommandSpec("stdout", "stdout <file|off>", "console", "capture V30 stdout", "console"),
    CommandSpec("ls", "ls [flash:/|sd:/path]", "files", "list a shared filesystem directory", "filesystem"),
    CommandSpec("cat", "cat <flash:|sd:/file>", "files", "read a shared file", "filesystem"),
    CommandSpec("put", "put <host-file> <flash:|sd:/path>", "files", "upload a Host file", "filesystem"),
    CommandSpec("get", "get <flash:|sd:/file> [host-file]", "files", "download a shared file", "filesystem"),
    CommandSpec("rm", "rm <flash:|sd:/path>", "files", "remove a shared file", "filesystem"),
    CommandSpec("mv", "mv <source> <destination>", "files", "rename a shared file", "filesystem"),
    CommandSpec("df", "df [flash:|sd:]", "storage", "show storage capacity and availability", "storage"),
    CommandSpec("mount", "mount sd:", "storage", "mount removable SD storage", "sd"),
    CommandSpec("unmount", "unmount sd:", "storage", "flush and unmount SD storage", "sd"),
    CommandSpec("sync", "sync [flash:|sd:]", "storage", "flush persistent filesystem state", "storage"),
    CommandSpec("mem", "mem read|write|load|save ...", "memory", "inspect or change V30-visible memory", "memory"),
    CommandSpec("trace", "trace on|off|save [file]", "observe", "control and save bus trace", "trace"),
    CommandSpec("selftest", "selftest [psram|all]", "observe", "run a canonical runtime resource self-test", "selftest"),
    CommandSpec("timeout", "timeout [seconds|off]", "control", "show or set the workload watchdog", "watchdog"),
    CommandSpec("regs", "regs", "observe", "show workload-published V30 registers", "registers"),
    CommandSpec("quiet", "quiet", "shell", "show errors and command results only"),
    CommandSpec("verbose", "verbose", "shell", "show every heartbeat result"),
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


def parse_command(line: str) -> ShellCommand | None:
    """Parse one shell line without executing or claiming a device operation."""
    try:
        words = shlex.split(line, posix=True)
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
