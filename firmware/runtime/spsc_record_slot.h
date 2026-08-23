/*
 * One-record SPSC ownership slot for the fixed 64-byte Host Bridge ABI.
 *
 * The producer publishes only after copying a complete record. The consumer
 * releases the slot only after copying that immutable record. A full slot is
 * never overwritten: the producer records a drop and returns false.
 */
#ifndef PI86_RUNTIME_SPSC_RECORD_SLOT_H
#define PI86_RUNTIME_SPSC_RECORD_SLOT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ai_bridge/bridge_protocol.h"

#ifndef PI86_SPSC_RECORD_BARRIER
#include "hardware/sync.h"
#define PI86_SPSC_RECORD_BARRIER() __dmb()
#endif

typedef struct {
    uint8_t record[PI86_BRIDGE_MESSAGE_SIZE];
    volatile bool ready;
    volatile uint32_t producer_drops;
} pi86_spsc_record_slot_t;

static inline bool pi86_spsc_record_try_publish(
    pi86_spsc_record_slot_t *slot,
    const uint8_t record[PI86_BRIDGE_MESSAGE_SIZE]) {
    if (slot->ready) {
        slot->producer_drops++;
        return false;
    }

    memcpy(slot->record, record, PI86_BRIDGE_MESSAGE_SIZE);
    PI86_SPSC_RECORD_BARRIER();
    slot->ready = true;
    return true;
}

static inline bool pi86_spsc_record_try_take(
    pi86_spsc_record_slot_t *slot,
    uint8_t record[PI86_BRIDGE_MESSAGE_SIZE]) {
    if (!slot->ready) return false;

    PI86_SPSC_RECORD_BARRIER();
    memcpy(record, slot->record, PI86_BRIDGE_MESSAGE_SIZE);
    PI86_SPSC_RECORD_BARRIER();
    slot->ready = false;
    return true;
}

static inline uint32_t pi86_spsc_record_drops(
    const pi86_spsc_record_slot_t *slot) {
    return slot->producer_drops;
}

#endif
