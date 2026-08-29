#ifndef RP86_WORKLOAD_MANAGER_H
#define RP86_WORKLOAD_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/backing.h"
#include "runtime/workload_protocol.h"

/*
 * Stages one flat 8086-class image in a processor-addressed backing resource.
 *
 * This manager deliberately stops at the physical timing boundary.  It owns
 * upload integrity and lifecycle state; the active bus controller
 * owns reset handoff and current-cycle processor responses.  Keeping
 * that boundary explicit prevents a successful upload from being reported as
 * successful native execution.
 */
typedef struct {
    rp86_memory_backing_t *backing;
    rp86_workload_manifest_t manifest;
    uint32_t transfer_id;
    uint32_t workload_id;
    uint32_t received;
    uint32_t running_crc32;
    rp86_workload_state_t state;
} rp86_workload_manager_t;

void rp86_workload_manager_init(rp86_workload_manager_t *manager,
                                rp86_memory_backing_t *backing);
bool rp86_workload_begin(rp86_workload_manager_t *manager,
                         uint32_t transfer_id,
                         const rp86_workload_manifest_t *manifest);
bool rp86_workload_write(rp86_workload_manager_t *manager,
                         uint32_t transfer_id, uint32_t offset,
                         const uint8_t *data, size_t length);
bool rp86_workload_commit(rp86_workload_manager_t *manager,
                          uint32_t transfer_id, uint32_t expected_crc32);
bool rp86_workload_run(rp86_workload_manager_t *manager,
                       uint32_t workload_id);
bool rp86_workload_stop(rp86_workload_manager_t *manager,
                        uint32_t workload_id);
bool rp86_workload_restart(rp86_workload_manager_t *manager,
                           uint32_t workload_id);
void rp86_workload_discard(rp86_workload_manager_t *manager);
const char *rp86_workload_state_name(rp86_workload_state_t state);

#endif
