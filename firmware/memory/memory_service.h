#ifndef RP86_MEMORY_SERVICE_H
#define RP86_MEMORY_SERVICE_H

#include <stdbool.h>

#include "host_protocol/host_protocol.h"
#include "memory/backing.h"

typedef struct {
    rp86_memory_backing_t *backing;
} rp86_memory_service_t;

void rp86_memory_service_init(rp86_memory_service_t *service,
                              rp86_memory_backing_t *backing);
bool rp86_memory_service_handle(
    rp86_memory_service_t *service,
    const rp86_host_protocol_message_t *request,
    rp86_host_protocol_message_t *reply);

#endif
