#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "memory/memory.h"
#include "processor/processor_bus.h"

typedef bool (*rp86_clock_stepped_io_read_fn)(void *context, uint16_t port,
                                     rp86_processor_bus_lanes_t lanes, uint16_t *value);
typedef bool (*rp86_clock_stepped_io_write_fn)(void *context, uint16_t port,
                                      rp86_processor_bus_lanes_t lanes, uint16_t value);

typedef struct {
    void *context;
    rp86_clock_stepped_io_read_fn read;
    rp86_clock_stepped_io_write_fn write;
} rp86_clock_stepped_io_t;

typedef struct {
    uint32_t cycles;
    uint32_t memory_reads;
    uint32_t memory_writes;
    uint32_t io_reads;
    uint32_t io_writes;
    uint32_t first_address;
    uint32_t last_address;
    rp86_processor_bus_cycle_type_t last_type;
    rp86_processor_bus_lanes_t last_lanes;
    uint16_t last_data;
    bool last_data_valid;
    bool first_cycle_seen;
    bool unmapped;
    bool invalid_lane;
    bool pad_mismatch;
    bool no_cycle;
    bool clock_failure;
    bool interrupt_ack;
} rp86_clock_stepped_stats_t;

/*
 * Service one complete clock-stepped physical processor bus cycle.
 *
 * The caller advances no processor clock while memory or I/O policy is being
 * evaluated. rp86_processor_bus owns the established Pi86 HAT phase cadence and this
 * layer owns byte-lane-aware access to the supplied backends.
 */
bool rp86_clock_stepped_service_cycle(rp86_processor_bus_t *bus,
                              rp86_memory_t *memory,
                              const rp86_clock_stepped_io_t *io,
                              uint max_idle_steps,
                              rp86_clock_stepped_stats_t *stats);

const char *rp86_clock_stepped_cycle_name(rp86_processor_bus_cycle_type_t type);
