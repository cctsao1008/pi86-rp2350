#ifndef RP86_MEMORY_BACKING_H
#define RP86_MEMORY_BACKING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Backing-neutral storage presented through 8086-class physical addresses.
 *
 * Host protocol and workload code use processor addresses.  Only this private
 * RP2350 layer translates them to Internal SRAM, PSRAM, or a future cache.
 * Keeping the translation here prevents the public workload ABI from naming a
 * physical memory device.
 */

typedef bool (*rp86_backing_read_fn)(void *context, size_t offset,
                                     void *destination, size_t length);
typedef bool (*rp86_backing_write_fn)(void *context, size_t offset,
                                      const void *source, size_t length);
typedef bool (*rp86_backing_fill_fn)(void *context, size_t offset,
                                     uint8_t value, size_t length);
typedef void (*rp86_backing_sync_fn)(void *context);

typedef struct {
    const char *name;
    void *context;
    uint32_t processor_base;
    size_t size;
    bool available;
    rp86_backing_read_fn read;
    rp86_backing_write_fn write;
    rp86_backing_fill_fn fill;
    rp86_backing_sync_fn publish;
    rp86_backing_sync_fn invalidate;
} rp86_memory_backing_t;

/* Initialize a directly addressable byte array as one processor-visible
 * region. This is the implementation used by the Internal SRAM tier and is
 * also useful for deterministic host-side tests. */
void rp86_memory_backing_init_direct(rp86_memory_backing_t *backing,
                                     const char *name,
                                     uint32_t processor_base,
                                     uint8_t *storage,
                                     size_t size);

bool rp86_memory_backing_range_valid(const rp86_memory_backing_t *backing,
                                     uint32_t processor_address,
                                     size_t length);
bool rp86_memory_backing_read(const rp86_memory_backing_t *backing,
                              uint32_t processor_address,
                              void *destination, size_t length);
bool rp86_memory_backing_write(rp86_memory_backing_t *backing,
                               uint32_t processor_address,
                               const void *source, size_t length);
bool rp86_memory_backing_fill(rp86_memory_backing_t *backing,
                              uint32_t processor_address,
                              uint8_t value, size_t length);
void rp86_memory_backing_publish(rp86_memory_backing_t *backing);
void rp86_memory_backing_invalidate(rp86_memory_backing_t *backing);

#endif
