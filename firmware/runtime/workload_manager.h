#ifndef PI86_WORKLOAD_MANAGER_H
#define PI86_WORKLOAD_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "memory/psram_backing.h"
#include "runtime/workload_protocol.h"

/*
 * Stages one flat 8086-class image in external PSRAM.
 *
 * This manager deliberately stops at the physical timing boundary.  It owns
 * upload integrity and lifecycle state; the future arbitrary-address bus
 * engine owns reset handoff and current-cycle V30/8086 responses.  Keeping
 * that boundary explicit prevents a successful upload from being reported as
 * successful native execution.
 */
typedef struct {
    pi86_psram_backing_t *backing;
    pi86_workload_manifest_t manifest;
    uint32_t transfer_id;
    uint32_t workload_id;
    uint32_t received;
    uint32_t running_crc32;
    pi86_workload_state_t state;
} pi86_workload_manager_t;

void pi86_workload_manager_init(pi86_workload_manager_t *manager,
                                pi86_psram_backing_t *backing);
bool pi86_workload_begin(pi86_workload_manager_t *manager,
                         uint32_t transfer_id,
                         const pi86_workload_manifest_t *manifest);
bool pi86_workload_write(pi86_workload_manager_t *manager,
                         uint32_t transfer_id, uint32_t offset,
                         const uint8_t *data, size_t length);
bool pi86_workload_commit(pi86_workload_manager_t *manager,
                          uint32_t transfer_id, uint32_t expected_crc32);
void pi86_workload_discard(pi86_workload_manager_t *manager);
const char *pi86_workload_state_name(pi86_workload_state_t state);

#endif
