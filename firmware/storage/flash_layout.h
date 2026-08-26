#ifndef PI86_FLASH_LAYOUT_H
#define PI86_FLASH_LAYOUT_H

#include <stdint.h>

#define PI86_PRIMARY_FLASH_BYTES       (16u * 1024u * 1024u)
#define PI86_FIRMWARE_RESERVED_BYTES    (4u * 1024u * 1024u)
#define PI86_FLASH_VOLUME_OFFSET        PI86_FIRMWARE_RESERVED_BYTES
#define PI86_FLASH_VOLUME_BYTES        (12u * 1024u * 1024u)
#define PI86_FLASH_SECTOR_BYTES         512u
#define PI86_FLASH_ERASE_BYTES          4096u
#define PI86_FLASH_CLUSTER_BYTES        2048u
#define PI86_FLASH_VOLUME_SECTORS       \
    (PI86_FLASH_VOLUME_BYTES / PI86_FLASH_SECTOR_BYTES)
#define PI86_FLASH_ERASE_SECTORS        \
    (PI86_FLASH_ERASE_BYTES / PI86_FLASH_SECTOR_BYTES)

_Static_assert(PI86_FLASH_VOLUME_OFFSET % PI86_FLASH_ERASE_BYTES == 0u,
               "flash: offset must be erase-sector aligned");
_Static_assert(PI86_FLASH_VOLUME_BYTES % PI86_FLASH_ERASE_BYTES == 0u,
               "flash: size must be erase-sector aligned");
_Static_assert(PI86_FIRMWARE_RESERVED_BYTES + PI86_FLASH_VOLUME_BYTES ==
                   PI86_PRIMARY_FLASH_BYTES,
               "flash layout must cover the 16 MiB primary NOR exactly");

#endif
