#include "memory/psram_backing.h"

#include <string.h>

#if PI86_HAS_EXTERNAL_PSRAM
#include "hardware/psram.h"
#include "hardware/regs/addressmap.h"
#include "hardware/xip_cache.h"

enum {
    /* QMI memory window 1 is the SDK-defined PSRAM mapping. */
    PI86_PSRAM_WINDOW_OFFSET = 0x01000000u,
};
#endif

bool pi86_psram_backing_init(pi86_psram_backing_t *backing) {
    if (backing == NULL) return false;
    backing->base = NULL;
    backing->size = 0u;
    backing->available = false;

#if !PI86_HAS_EXTERNAL_PSRAM
    return false;
#else
    if (!psram_is_available()) return false;
    const size_t size = psram_get_size();
    if (size == 0u) return false;

    uint8_t *const base = (uint8_t *)(XIP_BASE + PI86_PSRAM_WINDOW_OFFSET);
    if (!psram_check_address(base) ||
        !psram_check_address(base + size - 1u))
        return false;

    backing->base = base;
    backing->size = size;
    backing->available = true;
    return true;
#endif
}

bool pi86_psram_range_valid(const pi86_psram_backing_t *backing,
                            size_t offset, size_t length) {
    return backing != NULL && backing->available &&
           offset <= backing->size && length <= backing->size - offset;
}

bool pi86_psram_write(pi86_psram_backing_t *backing, size_t offset,
                      const void *source, size_t length) {
    if (!pi86_psram_range_valid(backing, offset, length) ||
        (length != 0u && source == NULL))
        return false;
    memcpy(backing->base + offset, source, length);
    return true;
}

bool pi86_psram_read(const pi86_psram_backing_t *backing, size_t offset,
                     void *destination, size_t length) {
    if (!pi86_psram_range_valid(backing, offset, length) ||
        (length != 0u && destination == NULL))
        return false;
    memcpy(destination, backing->base + offset, length);
    return true;
}

bool pi86_psram_fill(pi86_psram_backing_t *backing, size_t offset,
                     uint8_t value, size_t length) {
    if (!pi86_psram_range_valid(backing, offset, length)) return false;
    memset(backing->base + offset, value, length);
    return true;
}

void pi86_psram_publish(void) {
#if PI86_HAS_EXTERNAL_PSRAM
    xip_cache_clean_all();
#endif
}

void pi86_psram_invalidate(void) {
#if PI86_HAS_EXTERNAL_PSRAM
    xip_cache_invalidate_all();
#endif
}
