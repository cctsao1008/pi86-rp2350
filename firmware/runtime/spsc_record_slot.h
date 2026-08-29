/*
 * One-record SPSC ownership slot for the fixed 64-byte Host Bridge ABI.
 *
 * The producer publishes only after copying a complete record. The consumer
 * releases the slot only after copying that immutable record. A full slot is
 * never overwritten: the producer records a drop and returns false.
 */
#ifndef RP86_RUNTIME_SPSC_RECORD_SLOT_H
#define RP86_RUNTIME_SPSC_RECORD_SLOT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "host_protocol/host_protocol.h"

#ifndef RP86_SPSC_RECORD_BARRIER
#include "hardware/sync.h"
#define RP86_SPSC_RECORD_BARRIER() __dmb()
#endif

typedef struct {
    uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE];
    volatile bool ready;
    volatile uint32_t producer_drops;
} rp86_spsc_record_slot_t;

static inline bool rp86_spsc_record_try_publish(
    rp86_spsc_record_slot_t *slot,
    const uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE]) {
    if (slot->ready) {
        slot->producer_drops++;
        return false;
    }

    memcpy(slot->record, record, RP86_HOST_PROTOCOL_MESSAGE_SIZE);
    RP86_SPSC_RECORD_BARRIER();
    slot->ready = true;
    return true;
}

static inline bool rp86_spsc_record_try_take(
    rp86_spsc_record_slot_t *slot,
    uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE]) {
    if (!slot->ready) return false;

    RP86_SPSC_RECORD_BARRIER();
    memcpy(record, slot->record, RP86_HOST_PROTOCOL_MESSAGE_SIZE);
    RP86_SPSC_RECORD_BARRIER();
    slot->ready = false;
    return true;
}

static inline uint32_t rp86_spsc_record_drops(
    const rp86_spsc_record_slot_t *slot) {
    return slot->producer_drops;
}

#endif
