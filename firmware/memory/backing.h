#ifndef PI86_MEMORY_BACKING_H
#define PI86_MEMORY_BACKING_H

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

typedef bool (*pi86_backing_read_fn)(void *context, size_t offset,
                                     void *destination, size_t length);
typedef bool (*pi86_backing_write_fn)(void *context, size_t offset,
                                      const void *source, size_t length);
typedef bool (*pi86_backing_fill_fn)(void *context, size_t offset,
                                     uint8_t value, size_t length);
typedef void (*pi86_backing_sync_fn)(void *context);

typedef struct {
    const char *name;
    void *context;
    uint32_t processor_base;
    size_t size;
    bool available;
    pi86_backing_read_fn read;
    pi86_backing_write_fn write;
    pi86_backing_fill_fn fill;
    pi86_backing_sync_fn publish;
    pi86_backing_sync_fn invalidate;
} pi86_memory_backing_t;

/* Initialize a directly addressable byte array as one processor-visible
 * region. This is the implementation used by the Internal SRAM tier and is
 * also useful for deterministic host-side tests. */
void pi86_memory_backing_init_direct(pi86_memory_backing_t *backing,
                                     const char *name,
                                     uint32_t processor_base,
                                     uint8_t *storage,
                                     size_t size);

bool pi86_memory_backing_range_valid(const pi86_memory_backing_t *backing,
                                     uint32_t processor_address,
                                     size_t length);
bool pi86_memory_backing_read(const pi86_memory_backing_t *backing,
                              uint32_t processor_address,
                              void *destination, size_t length);
bool pi86_memory_backing_write(pi86_memory_backing_t *backing,
                               uint32_t processor_address,
                               const void *source, size_t length);
bool pi86_memory_backing_fill(pi86_memory_backing_t *backing,
                              uint32_t processor_address,
                              uint8_t value, size_t length);
void pi86_memory_backing_publish(pi86_memory_backing_t *backing);
void pi86_memory_backing_invalidate(pi86_memory_backing_t *backing);

#endif
