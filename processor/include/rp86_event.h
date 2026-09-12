#ifndef RP86_EVENT_H
#define RP86_EVENT_H

#include <stdint.h>

/*
 * Minimal processor-owned event witness.
 *
 * A producer writes event/arg first and publishes seq last.  A Host observer
 * may read seq before and after the surrounding telemetry block and retry when
 * the two values differ.  The record intentionally carries no strings,
 * timestamp, ring-buffer state, or transport ownership.
 */
typedef struct rp86_event
{
    volatile uint16_t seq;
    volatile uint16_t event;
    volatile uint16_t arg;
} rp86_event_t;

#endif /* RP86_EVENT_H */
