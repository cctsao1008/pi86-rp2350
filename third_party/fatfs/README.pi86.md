# FatFs integration for `flash:/`

This directory vendors FatFs R0.16 with the official patch 1 and patch 2
applied. `source/ffconf.h` is the pi86-rp2350 configuration.

The canonical `RP-FLASH` volume is a super-floppy FAT16 filesystem in the final
12 MiB of the 16 MiB W25Q128JV primary NOR flash:

- firmware reserve: `0x000000`-`0x3FFFFF` (4 MiB)
- `flash:/`: `0x400000`-`0xFFFFFF` (12 MiB)
- logical sector: 512 bytes
- allocation unit: 2 KiB
- physical erase unit: 4 KiB (8 logical sectors)

The RP2350 is the only filesystem owner. Host and physical 8086-class
processors access files through mediated runtime services; they do not mount
the block device concurrently.

Upstream: <https://elm-chan.org/fsw/ff/>
