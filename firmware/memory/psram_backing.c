#include "memory/psram_backing.h"

#include <string.h>

#if RP86_HAS_EXTERNAL_PSRAM
#include "hardware/psram.h"
#include "hardware/regs/addressmap.h"
#include "hardware/xip_cache.h"

enum {
    /* QMI memory window 1 is the SDK-defined PSRAM mapping. */
    RP86_PSRAM_WINDOW_OFFSET = 0x01000000u,
};
#endif

bool rp86_psram_backing_init(rp86_psram_backing_t *backing) {
    if (backing == NULL) return false;
    backing->base = NULL;
    backing->size = 0u;
    backing->available = false;

#if !RP86_HAS_EXTERNAL_PSRAM
    return false;
#else
    if (!psram_is_available()) return false;
    const size_t size = psram_get_size();
    if (size == 0u) return false;

    uint8_t *const base = (uint8_t *)(XIP_BASE + RP86_PSRAM_WINDOW_OFFSET);
    if (!psram_check_address(base) ||
        !psram_check_address(base + size - 1u))
        return false;

    backing->base = base;
    backing->size = size;
    backing->available = true;
    return true;
#endif
}

bool rp86_psram_range_valid(const rp86_psram_backing_t *backing,
                            size_t offset, size_t length) {
    return backing != NULL && backing->available &&
           offset <= backing->size && length <= backing->size - offset;
}

bool rp86_psram_write(rp86_psram_backing_t *backing, size_t offset,
                      const void *source, size_t length) {
    if (!rp86_psram_range_valid(backing, offset, length) ||
        (length != 0u && source == NULL))
        return false;
    memcpy(backing->base + offset, source, length);
    return true;
}

bool rp86_psram_read(const rp86_psram_backing_t *backing, size_t offset,
                     void *destination, size_t length) {
    if (!rp86_psram_range_valid(backing, offset, length) ||
        (length != 0u && destination == NULL))
        return false;
    memcpy(destination, backing->base + offset, length);
    return true;
}

bool rp86_psram_fill(rp86_psram_backing_t *backing, size_t offset,
                     uint8_t value, size_t length) {
    if (!rp86_psram_range_valid(backing, offset, length)) return false;
    memset(backing->base + offset, value, length);
    return true;
}

void rp86_psram_publish(void) {
#if RP86_HAS_EXTERNAL_PSRAM
    xip_cache_clean_all();
#endif
}

void rp86_psram_invalidate(void) {
#if RP86_HAS_EXTERNAL_PSRAM
    xip_cache_invalidate_all();
#endif
}
