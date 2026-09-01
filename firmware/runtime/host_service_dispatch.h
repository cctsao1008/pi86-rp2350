#ifndef RP86_HOST_SERVICE_DISPATCH_H
#define RP86_HOST_SERVICE_DISPATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "host_protocol/host_protocol.h"
#include "memory/backing.h"
#include "memory/memory_service.h"
#include "runtime/workload_manager.h"
#include "storage/flash_service.h"

typedef struct {
    rp86_flash_service_t *flash;
    rp86_memory_service_t *memory;
    rp86_workload_manager_t *workload;
    rp86_memory_backing_t *workload_memory;
    uint32_t reply_timeout_us;
} rp86_host_service_dispatch_t;

typedef struct {
    bool handled;
    bool replied;
    uint8_t operation;
    uint32_t address;
    uint32_t length;
    rp86_host_protocol_status_t status;
} rp86_host_service_result_t;

rp86_host_service_result_t rp86_host_service_dispatch_filesystem(
    rp86_host_service_dispatch_t *dispatch,
    const rp86_host_protocol_message_t *request);

rp86_host_service_result_t rp86_host_service_dispatch_memory(
    rp86_host_service_dispatch_t *dispatch,
    const rp86_host_protocol_message_t *request);

#endif
