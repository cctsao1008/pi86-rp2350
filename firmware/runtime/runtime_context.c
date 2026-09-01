#include "runtime/runtime_context.h"

#include <string.h>

#include "memory/internal_sram_backing.h"
#include "memory/shared_mailbox.h"

bool rp86_runtime_context_init(rp86_runtime_context_t *context,
                               uint32_t host_reply_timeout_us) {
    memset(context, 0, sizeof *context);
    rp86_internal_sram_backing_init(&context->memory_backing);
    if (!rp86_shared_mailbox_init(&context->memory_backing)) return false;

    rp86_memory_service_init(&context->memory, &context->memory_backing);
    rp86_workload_manager_init(&context->workload,
                               &context->memory_backing);
    context->flash_available =
        rp86_flash_volume_init(&context->flash_volume);
    rp86_flash_service_init(&context->flash, &context->flash_volume,
                            context->flash_available);
    context->host_services = (rp86_host_service_dispatch_t){
        .flash = &context->flash,
        .memory = &context->memory,
        .workload = &context->workload,
        .workload_memory = &context->memory_backing,
        .reply_timeout_us = host_reply_timeout_us,
    };
    return true;
}
