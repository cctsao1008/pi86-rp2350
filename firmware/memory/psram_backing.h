#ifndef PI86_PSRAM_BACKING_H
#define PI86_PSRAM_BACKING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * External PSRAM is a bulk backing resource, not an active V30-cycle
 * responder.  Callers must hold the resource lease and keep the other RP2350
 * core out of XIP/PSRAM ownership transitions while publishing or
 * invalidating cached data.
 */

typedef struct {
    uint8_t *base;
    size_t size;
    bool available;
} pi86_psram_backing_t;

bool pi86_psram_backing_init(pi86_psram_backing_t *backing);
bool pi86_psram_range_valid(const pi86_psram_backing_t *backing,
                            size_t offset, size_t length);
bool pi86_psram_write(pi86_psram_backing_t *backing, size_t offset,
                      const void *source, size_t length);
bool pi86_psram_read(const pi86_psram_backing_t *backing, size_t offset,
                     void *destination, size_t length);
bool pi86_psram_fill(pi86_psram_backing_t *backing, size_t offset,
                     uint8_t value, size_t length);

/* Publish CPU writes to external PSRAM before DMA/another owner consumes
 * them.  The current SDK implementation cleans the complete XIP cache because
 * that is faster and safer than walking a multi-megabyte backing range. */
void pi86_psram_publish(void);

/* Discard cached lines after another owner may have changed external PSRAM.
 * Always publish dirty CPU writes before invalidating. */
void pi86_psram_invalidate(void);

#endif
