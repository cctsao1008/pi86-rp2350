#ifndef RP86_EVENT_H
#define RP86_EVENT_H

#include <stdint.h>

/*
 * Minimal processor-owned event witness.
 *
 * seq also acts as a tiny seqlock for the surrounding telemetry block:
 * odd means update in progress, even means stable.  The processor publishes
 * no strings, timestamps, ring-buffer state, or transport ownership here.
 */
typedef struct rp86_event
{
    volatile uint16_t seq;
    volatile uint16_t event;
    volatile uint16_t arg;
} rp86_event_t;

#endif /* RP86_EVENT_H */
