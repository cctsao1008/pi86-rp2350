# ELKS V30 FD1440 Bring-up Record

Date: 2026-08-24  
Project: `pi86-rp2350`  
ELKS submodule: `third_party/elks`  
Pinned ELKS revision: `684a5645f4a2e68267062ef37f4827c66d9d42fe`

## Purpose

Record the first successful ELKS build baseline for the `pi86-rp2350` project and the verified boot requirements that will be used for real NEC V30 hardware bring-up.

The target is native execution on a physical NEC V30. RP2350 provides the software-defined chipset and BIOS-facing services.

---

## ELKS Integration Baseline

ELKS is included as a pinned Git submodule:

```text
third_party/elks
```

The project-specific ELKS configuration is stored in the main repository as:

```text
configs/elks/pi86-v30-fd1440.config
```

The generated disk image itself should remain a build artifact and should not be committed.

---

## Selected ELKS Configuration

### Architecture

```text
System: IBM-PC
CPU tuning: i8086
```

Relevant kernel configuration:

```text
# CONFIG_ASYNCIO is not set

CONFIG_BLK_DEV_BFD=y
# CONFIG_BLK_DEV_FD is not set
# CONFIG_BLK_DEV_BFD_HARD is not set

CONFIG_CONSOLE_HEADLESS=y
# CONFIG_CONSOLE_DIRECT is not set
# CONFIG_CONSOLE_BIOS is not set
# CONFIG_CONSOLE_SERIAL is not set

# CONFIG_ETH is not set
```

### Filesystem

```text
Minix filesystem: enabled
FAT filesystem: enabled
ROM filesystem: disabled
External buffer cache: enabled
XMS: disabled
```

### Target Image

```text
Filesystem: FAT
Medium: FD1440
Bootable: yes
Compressed executables: disabled
Extra binary images: disabled
```

The first bring-up image is therefore:

```text
FAT12
1.44 MB floppy geometry
bootable
```

---

## Build Issue Encountered

The first build failed while compiling:

```text
directfd.c
```

with:

```text
#define check_disk_change(dev) 0
```

conflicting with:

```c
int check_disk_change(kdev_t dev)
```

The root cause was:

```text
CONFIG_ASYNCIO=y
```

At the pinned ELKS revision, the block-driver Makefile adds `directfd.o` whenever `CONFIG_ASYNCIO` is enabled, even when direct hardware floppy support is disabled.

Resolution:

```text
[ ] Use Async I/O in kernel
```

After disabling `CONFIG_ASYNCIO`, the clean build completed successfully.

---

## Successful Build Output

Generated kernel:

```text
target/linux
73720 bytes
```

Generated boot image:

```text
image/DESKTOP-M2HSQ30.img
1474560 bytes
```

Image size:

```text
1474560 bytes
= 2880 sectors × 512 bytes
= standard 1.44 MB floppy image
```

---

## Image Verification

`file` identified the image as:

```text
DOS/MBR boot sector
OEM-ID "ELKSFAT1"
FAT12
root entries 224
sectors 2880
sectors/FAT 9
sectors/track 18
```

First bytes:

```text
00000000: eb3c 9045 4c4b 5346 4154 3100 0201 0100
00000010: 02e0 0040 0bf0 0900 1200 0200 0000 0000
00000020: 0000 0000 0000 2900 0000 004e 4f20 4e41
00000030: 4d45 2020 2020 4641 5431 3220 2020 cd12
```

Boot signature:

```text
000001fe: 55aa
```

FAT root contents were verified with `mdir`:

```text
/dev
/bin
/bootopts
/etc
/home
/mnt
/root
/tmp
```

The image had approximately:

```text
771584 bytes free
```

---

## Verified ELKS Boot Flow

For the FAT boot configuration, ELKS uses a single-sector FAT bootloader.

The first-stage boot flow is:

```text
NEC V30 RESET
    |
    v
pi86 BIOS
    |
    v
Load sector 0 to 0000:7C00
    |
    v
ELKS FAT boot sector
    |
    +--> INT 12h
    |     query conventional memory size
    |
    +--> INT 10h / AH=0Eh
    |     teletype output
    |
    +--> FAT12 traversal
    |
    +--> INT 13h / AH=02h
    |     read sectors
    |
    v
Load /LINUX
    |
    v
ELKS setup.S
    |
    v
ELKS kernel
```

The boot sector expects:

```text
entry address: 0000:7C00
DL: BIOS boot drive number
```

---

## Minimum BIOS Contract for First Bring-up

### Required

| BIOS Service | Purpose |
|---|---|
| `INT 12h` | Return conventional memory size |
| `INT 10h AH=0Eh` | Teletype output |
| `INT 13h AH=02h` | Read disk sectors |
| `INT 13h AH=00h` | Reset disk after read failure |
| `DL` boot drive | Identify boot device |

### Useful for Error Handling

| BIOS Service | Purpose |
|---|---|
| `INT 16h AH=00h` | Wait for key on boot failure path |

---

## Virtual FD1440 Geometry

The RP2350-side BIOS disk backend can expose the ELKS image as:

```text
Cylinders:       80
Heads:            2
Sectors/track:   18
Bytes/sector:   512
Total sectors: 2880
```

CHS-to-LBA conversion:

```text
LBA = ((cylinder * 2 + head) * 18) + (sector - 1)
```

For the FAT bootloader, the early path reads one sector at a time, which keeps the first implementation simple.

---

## RP2350 Virtual Disk Model

Initial disk-read path:

```text
INT 13h AH=02h
        |
        v
CHS request
        |
        v
CHS -> LBA
        |
        v
image[LBA * 512]
        |
        v
copy 512 bytes to V30 ES:BX
        |
        v
CF = 0
AH = 0
```

The physical backend used by RP2350 is intentionally hidden from ELKS.

Possible RP2350 storage backends later include:

```text
Flash
PSRAM
microSD
host-provided image
```

ELKS continues to see a BIOS disk.

---

## ELKS V30 Support

ELKS setup code explicitly distinguishes CPU types including:

```text
0 = 8088
1 = 8086
2 = NEC V20
3 = NEC V30
4 = 80188
5 = 80186
6 = 80286
7 = 80386+
```

This confirms that NEC V30 is an explicitly recognized ELKS processor type rather than merely relying on generic 8086 compatibility.

---

## Bring-up Gates

### E0 — ELKS Build Baseline

Status:

```text
PASS
```

Criteria satisfied:

```text
ELKS source builds successfully
IBM-PC / i8086 configuration
FAT12 FD1440 image produced
boot signature verified
filesystem contents verified
```

### E1 — Native ELKS Boot-Sector Execution

Goal:

```text
BIOS
  -> load sector 0
  -> jump to 0000:7C00
  -> ELKS boot sector executes on real V30
  -> INT 12h succeeds
  -> INT 10h AH=0Eh succeeds
  -> ELKS boot text appears through RP2350 debug/CDC path
```

PASS criterion:

```text
The first visible boot text must originate from the ELKS boot sector,
not from pi86 BIOS diagnostic output.
```

### E2 — ELKS FAT / Kernel Load

Goal:

```text
INT 13h
  -> FAT12 traversal
  -> locate /LINUX
  -> read required sectors
  -> transfer control to ELKS setup
```

### E3 — ELKS Kernel Console

Goal:

```text
setup
  -> kernel entry
  -> headless console
  -> visible kernel output through RP2350
```

### E4 — Timer and Interrupt Path

Goal:

```text
RP2350 timer
  -> virtual 8254 PIT
  -> IRQ0
  -> virtual 8259
  -> V30 INT / INTA
  -> ELKS timer ISR
```

### E5 — Shell

Goal:

```text
kernel
  -> root filesystem
  -> /bin/sh
  -> interactive shell prompt
```

---

## Current Decision

Do not further optimize ELKS userland or image size before hardware boot.

The next implementation target is:

```text
E1: native ELKS boot-sector execution on the physical NEC V30
```

The first hardware implementation should remain intentionally narrow:

```text
INT 12h
INT 10h AH=0Eh
boot sector load to 0000:7C00
correct DL boot drive value
```

`INT 13h` FAT loading can follow as E2 after E1 is proven.
