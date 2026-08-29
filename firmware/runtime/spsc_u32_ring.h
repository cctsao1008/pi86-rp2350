/*
 * Bounded single-producer/single-consumer uint32_t ring for RP2350 SRAM.
 *
 * One core owns write_index and one core owns read_index.  The producer must
 * never block when the ring is full: it records a drop and returns false.
 * The DMB operations publish payload data before its index and prevent the
 * consumer from reading an entry before observing that publication.
 */
#ifndef RP86_RUNTIME_SPSC_U32_RING_H
#define RP86_RUNTIME_SPSC_U32_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/sync.h"

#define RP86_SPSC_U32_CAPACITY 64u
#define RP86_SPSC_U32_MASK (RP86_SPSC_U32_CAPACITY - 1u)

#if (RP86_SPSC_U32_CAPACITY & RP86_SPSC_U32_MASK) != 0
#error "RP86_SPSC_U32_CAPACITY must be a power of two"
#endif

typedef struct {
    uint32_t entries[RP86_SPSC_U32_CAPACITY];
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    volatile uint32_t producer_drops;
} rp86_spsc_u32_ring_t;

static inline bool rp86_spsc_u32_try_push(rp86_spsc_u32_ring_t *ring,
                                           uint32_t value) {
    uint32_t write_index = ring->write_index;
    __dmb();
    uint32_t read_index = ring->read_index;
    if ((write_index - read_index) >= RP86_SPSC_U32_CAPACITY) {
        ring->producer_drops++;
        return false;
    }

    ring->entries[write_index & RP86_SPSC_U32_MASK] = value;
    __dmb();
    ring->write_index = write_index + 1u;
    return true;
}

static inline bool rp86_spsc_u32_try_pop(rp86_spsc_u32_ring_t *ring,
                                          uint32_t *value) {
    uint32_t read_index = ring->read_index;
    __dmb();
    uint32_t write_index = ring->write_index;
    if (read_index == write_index) return false;

    __dmb();
    *value = ring->entries[read_index & RP86_SPSC_U32_MASK];
    __dmb();
    ring->read_index = read_index + 1u;
    return true;
}

#endif
