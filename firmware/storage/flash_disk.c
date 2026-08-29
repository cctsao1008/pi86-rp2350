#include "storage/flash_disk.h"

#include <stdint.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "pico/flash.h"
#include "pico/platform.h"
#include "storage/flash_layout.h"

#define RP86_FLASH_DRIVE 0u
#define RP86_FLASH_SAFE_TIMEOUT_MS 10000u

typedef struct {
    uint32_t flash_offset;
    const uint8_t *data;
} flash_program_request_t;

static uint8_t g_cache[RP86_FLASH_ERASE_BYTES]
    __attribute__((aligned(RP86_FLASH_ERASE_BYTES)));
static uint32_t g_cache_block;
static bool g_cache_valid;
static bool g_cache_dirty;
static bool g_disk_initialized;
static bool g_disk_healthy = true;

_Static_assert(PICO_FLASH_SIZE_BYTES == RP86_PRIMARY_FLASH_BYTES,
               "board flash geometry must be 16 MiB");
_Static_assert(FLASH_PAGE_SIZE == 256u,
               "W25Q128JV page program geometry changed");
_Static_assert(FLASH_SECTOR_SIZE == RP86_FLASH_ERASE_BYTES,
               "W25Q128JV erase geometry changed");

static const uint8_t *flash_xip(uint32_t relative_offset) {
    return (const uint8_t *)(XIP_BASE + RP86_FLASH_VOLUME_OFFSET +
                             relative_offset);
}

static bool lba_range_valid(LBA_t sector, UINT count) {
    return count != 0u && sector < RP86_FLASH_VOLUME_SECTORS &&
           (LBA_t)count <= RP86_FLASH_VOLUME_SECTORS - sector;
}

static void __not_in_flash_func(program_erase_block)(void *parameter) {
    const flash_program_request_t *request =
        (const flash_program_request_t *)parameter;
    flash_range_erase(request->flash_offset, RP86_FLASH_ERASE_BYTES);
    flash_range_program(request->flash_offset, request->data,
                        RP86_FLASH_ERASE_BYTES);
}

bool rp86_flash_disk_flush(void) {
    if (!g_cache_dirty) return true;

    const uint32_t relative_offset = g_cache_block * RP86_FLASH_ERASE_BYTES;
    const uint8_t *existing = flash_xip(relative_offset);
    if (memcmp(existing, g_cache, sizeof g_cache) == 0) {
        g_cache_dirty = false;
        return true;
    }

    const flash_program_request_t request = {
        .flash_offset = RP86_FLASH_VOLUME_OFFSET + relative_offset,
        .data = g_cache,
    };
    const int result = flash_safe_execute(program_erase_block,
                                          (void *)&request,
                                          RP86_FLASH_SAFE_TIMEOUT_MS);
    if (result != PICO_OK || memcmp(flash_xip(relative_offset), g_cache,
                                    sizeof g_cache) != 0) {
        g_disk_healthy = false;
        return false;
    }

    g_cache_dirty = false;
    return true;
}

static bool select_cache_block(uint32_t block) {
    if (g_cache_valid && g_cache_block == block) return true;
    if (!rp86_flash_disk_flush()) return false;

    memcpy(g_cache, flash_xip(block * RP86_FLASH_ERASE_BYTES),
           sizeof g_cache);
    g_cache_block = block;
    g_cache_valid = true;
    return true;
}

bool rp86_flash_disk_healthy(void) {
    return g_disk_healthy;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != RP86_FLASH_DRIVE) return STA_NOINIT;
    g_disk_initialized = true;
    return 0u;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != RP86_FLASH_DRIVE || !g_disk_initialized) return STA_NOINIT;
    return g_disk_healthy ? 0u : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buffer, LBA_t sector, UINT count) {
    if (pdrv != RP86_FLASH_DRIVE || buffer == NULL ||
        !g_disk_initialized || !lba_range_valid(sector, count)) {
        return RES_PARERR;
    }

    for (UINT i = 0u; i < count; ++i) {
        const uint32_t byte_offset =
            (uint32_t)(sector + i) * RP86_FLASH_SECTOR_BYTES;
        const uint32_t block = byte_offset / RP86_FLASH_ERASE_BYTES;
        const uint32_t within = byte_offset % RP86_FLASH_ERASE_BYTES;
        const uint8_t *source =
            g_cache_valid && g_cache_block == block ? g_cache + within :
                                                       flash_xip(byte_offset);
        memcpy(buffer + i * RP86_FLASH_SECTOR_BYTES, source,
               RP86_FLASH_SECTOR_BYTES);
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buffer, LBA_t sector, UINT count) {
    if (pdrv != RP86_FLASH_DRIVE || buffer == NULL ||
        !g_disk_initialized || !lba_range_valid(sector, count)) {
        return RES_PARERR;
    }

    for (UINT i = 0u; i < count; ++i) {
        const uint32_t byte_offset =
            (uint32_t)(sector + i) * RP86_FLASH_SECTOR_BYTES;
        const uint32_t block = byte_offset / RP86_FLASH_ERASE_BYTES;
        const uint32_t within = byte_offset % RP86_FLASH_ERASE_BYTES;
        if (!select_cache_block(block)) return RES_ERROR;
        memcpy(g_cache + within, buffer + i * RP86_FLASH_SECTOR_BYTES,
               RP86_FLASH_SECTOR_BYTES);
        g_cache_dirty = true;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE command, void *buffer) {
    if (pdrv != RP86_FLASH_DRIVE || !g_disk_initialized) return RES_NOTRDY;

    switch (command) {
    case CTRL_SYNC:
        return rp86_flash_disk_flush() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (buffer == NULL) return RES_PARERR;
        *(LBA_t *)buffer = RP86_FLASH_VOLUME_SECTORS;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (buffer == NULL) return RES_PARERR;
        *(WORD *)buffer = RP86_FLASH_SECTOR_BYTES;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (buffer == NULL) return RES_PARERR;
        *(DWORD *)buffer = RP86_FLASH_ERASE_SECTORS;
        return RES_OK;
    case CTRL_TRIM:
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
