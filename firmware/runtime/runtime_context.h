#ifndef RP86_RUNTIME_CONTEXT_H
#define RP86_RUNTIME_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "memory/backing.h"
#include "memory/memory_service.h"
#include "runtime/host_service_dispatch.h"
#include "runtime/workload_manager.h"
#include "storage/flash_service.h"
#include "storage/flash_volume.h"

/*
 * Owns the non-real-time resources shared by the canonical runtime services.
 * ISR, PIO, DMA, and current-cycle bus state intentionally remain file-local
 * to their timing-critical implementation.
 */
typedef struct {
    rp86_memory_backing_t memory_backing;
    rp86_memory_service_t memory;
    rp86_workload_manager_t workload;
    rp86_flash_volume_t flash_volume;
    rp86_flash_service_t flash;
    bool flash_available;
    rp86_host_service_dispatch_t host_services;
} rp86_runtime_context_t;

bool rp86_runtime_context_init(rp86_runtime_context_t *context,
                               uint32_t host_reply_timeout_us);

#endif
