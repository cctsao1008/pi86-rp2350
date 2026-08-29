#include "memory/backing.h"

#include <string.h>

static bool direct_read(void *context, size_t offset,
                        void *destination, size_t length) {
    if (length == 0u) return true;
    if (length != 0u && (context == NULL || destination == NULL)) return false;
    memcpy(destination, (const uint8_t *)context + offset, length);
    return true;
}

static bool direct_write(void *context, size_t offset,
                         const void *source, size_t length) {
    if (length == 0u) return true;
    if (length != 0u && (context == NULL || source == NULL)) return false;
    memcpy((uint8_t *)context + offset, source, length);
    return true;
}

static bool direct_fill(void *context, size_t offset,
                        uint8_t value, size_t length) {
    if (length == 0u) return true;
    if (length != 0u && context == NULL) return false;
    memset((uint8_t *)context + offset, value, length);
    return true;
}

void rp86_memory_backing_init_direct(rp86_memory_backing_t *backing,
                                     const char *name,
                                     uint32_t processor_base,
                                     uint8_t *storage,
                                     size_t size) {
    if (backing == NULL) return;
    memset(backing, 0, sizeof *backing);
    backing->name = name;
    backing->context = storage;
    backing->processor_base = processor_base;
    backing->size = size;
    backing->available = storage != NULL && size != 0u;
    backing->read = direct_read;
    backing->write = direct_write;
    backing->fill = direct_fill;
}

bool rp86_memory_backing_range_valid(const rp86_memory_backing_t *backing,
                                     uint32_t processor_address,
                                     size_t length) {
    if (backing == NULL || !backing->available ||
        processor_address < backing->processor_base)
        return false;
    const size_t offset = (size_t)(processor_address - backing->processor_base);
    return offset <= backing->size && length <= backing->size - offset;
}

bool rp86_memory_backing_read(const rp86_memory_backing_t *backing,
                              uint32_t processor_address,
                              void *destination, size_t length) {
    if (!rp86_memory_backing_range_valid(backing, processor_address, length) ||
        backing->read == NULL || (length != 0u && destination == NULL))
        return false;
    const size_t offset = (size_t)(processor_address - backing->processor_base);
    return backing->read(backing->context, offset, destination, length);
}

bool rp86_memory_backing_write(rp86_memory_backing_t *backing,
                               uint32_t processor_address,
                               const void *source, size_t length) {
    if (!rp86_memory_backing_range_valid(backing, processor_address, length) ||
        backing->write == NULL || (length != 0u && source == NULL))
        return false;
    const size_t offset = (size_t)(processor_address - backing->processor_base);
    return backing->write(backing->context, offset, source, length);
}

bool rp86_memory_backing_fill(rp86_memory_backing_t *backing,
                              uint32_t processor_address,
                              uint8_t value, size_t length) {
    if (!rp86_memory_backing_range_valid(backing, processor_address, length) ||
        backing->fill == NULL)
        return false;
    const size_t offset = (size_t)(processor_address - backing->processor_base);
    return backing->fill(backing->context, offset, value, length);
}

void rp86_memory_backing_publish(rp86_memory_backing_t *backing) {
    if (backing != NULL && backing->available && backing->publish != NULL)
        backing->publish(backing->context);
}

void rp86_memory_backing_invalidate(rp86_memory_backing_t *backing) {
    if (backing != NULL && backing->available && backing->invalidate != NULL)
        backing->invalidate(backing->context);
}
