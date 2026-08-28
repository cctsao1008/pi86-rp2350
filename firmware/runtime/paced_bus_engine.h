#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "memory/memory.h"
#include "v30/v30_bus.h"

typedef bool (*pi86_paced_io_read_fn)(void *context, uint16_t port,
                                     v30_bus_lanes_t lanes, uint16_t *value);
typedef bool (*pi86_paced_io_write_fn)(void *context, uint16_t port,
                                      v30_bus_lanes_t lanes, uint16_t value);

typedef struct {
    void *context;
    pi86_paced_io_read_fn read;
    pi86_paced_io_write_fn write;
} pi86_paced_io_t;

typedef struct {
    uint32_t cycles;
    uint32_t memory_reads;
    uint32_t memory_writes;
    uint32_t io_reads;
    uint32_t io_writes;
    uint32_t first_address;
    uint32_t last_address;
    v30_bus_cycle_type_t last_type;
    bool first_cycle_seen;
    bool unmapped;
    bool invalid_lane;
    bool pad_mismatch;
} pi86_paced_stats_t;

/*
 * Service one complete software-paced physical processor bus cycle.
 *
 * The caller advances no processor clock while memory or I/O policy is being
 * evaluated.  v30_bus owns the established early-pi86 phase cadence and this
 * layer owns byte-lane-aware access to the supplied backends.
 */
bool pi86_paced_service_cycle(v30_bus_t *bus,
                              pi86_memory_t *memory,
                              const pi86_paced_io_t *io,
                              uint max_idle_steps,
                              pi86_paced_stats_t *stats);

const char *pi86_paced_cycle_name(v30_bus_cycle_type_t type);
