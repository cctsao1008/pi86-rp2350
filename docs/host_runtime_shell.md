# Host Runtime Shell

The Host runtime is a small remote shell for a real Intel 8086 or NEC V30. It is
not an operating system running on the processor. The Host provides control and
runtime services; the RP2350 owns resources and the physical bus; the processor executes
bare-metal native workloads.

The shell is the reference interface to the
**Host-Managed Bare-Metal Physical Processor Runtime**. It is not the wire
protocol and it is not required to be implemented in Python forever.

The shell shape is defined before every backend is complete so later PSRAM,
NOR Flash, SD Card, trace, and debugger work can attach to a stable interface.
An unavailable operation must return `NOT AVAILABLE`; it must never report a
successful hardware operation that did not occur.

## Resource model

```text
Host shell
   |
   +-- workload control ------ load / run / stop / restart
   +-- console --------------- send / stdin / stdout
   +-- live observation ------ status / top / trace / timeout
   +-- processor-visible memory -- mem read / write / load / save
   `-- RP2350-owned storage
          +-- flash: --------- built-in shared persistent FAT volume
          `-- sd: ------------ removable shared FAT volume
```

Both the Host and physical processor are clients of RP2350-owned memory and storage services.
Neither client directly owns a Flash controller, SD controller, filesystem
metadata, PSRAM allocator, or bus-engine state.

## Storage roles

- `flash:` is the built-in persistent FAT filesystem in External NOR Flash. It is
  intended for workloads, configuration, input, output, and persistent data.
- `sd:` is removable, larger-capacity FAT storage for workload libraries, datasets,
  trace export, snapshots, import, and backup.
- Removing or omitting SD must not prevent `flash:`, PSRAM execution, console,
  monitoring, or restart from operating.
- Filesystem requests are serialized by the RP2350. "Shared" means shared file
  contents and namespace, not concurrent raw-media ownership.

The public names are semantic and stable:

```text
flash:/workloads/hello.bin
sd:/datasets/input.dat
```

They are not DOS drive letters and do not expose Linux block-device names.
FatFs may use numeric logical drives internally, but values such as `0:` and
`1:` are private implementation details. The External NOR Flash volume requires
a Flash-aware block layer so FAT updates respect erase geometry and wear
management; an SD card uses its normal sector interface.

## Shell framework

| Area | Commands |
|---|---|
| Workload | `load`, `run`, `stop`, `restart` |
| Console | `send`, `console`, `stdin`, `stdout` |
| Files | `ls`, `cat`, `put`, `get`, `rm`, `mv` |
| Storage | `df`, `mount`, `unmount`, `sync` |
| Memory | `mem read`, `mem write`, `mem load`, `mem save` |
| Observation | `status`, `top`, `info`, `trace`, `regs` |
| Supervision | `ping`, `timeout`, heartbeat, restart |
| Shell | `help`, `quiet`, `verbose`, `quit` |

`top` describes one physical-CPU environment rather than an operating-system
process list. Its eventual fields include processor liveness and clock, active
workload, runtime, heartbeat latency/loss, PSRAM use, `flash:` and `sd:`
availability, open service handles, I/O counters, interrupt counts, bus errors,
watchdog state, and restart count.

The initial implementation may expose only heartbeat, status, display control,
and the existing bounded command exchange. Remaining commands are framework
contracts whose backends are enabled only after their physical capability is
implemented and validated.
