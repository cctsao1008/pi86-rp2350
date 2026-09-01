#include "runtime/host_service_dispatch.h"

#include <string.h>

#include "host_protocol/usb_transport.h"
#include "memory/shared_mailbox.h"

static bool send_reply(const rp86_host_service_dispatch_t *dispatch,
                       const rp86_host_protocol_message_t *reply) {
    return rp86_host_protocol_hid_send_record(
        (const uint8_t *)reply, dispatch->reply_timeout_us);
}

rp86_host_service_result_t rp86_host_service_dispatch_filesystem(
    rp86_host_service_dispatch_t *dispatch,
    const rp86_host_protocol_message_t *request) {
    rp86_host_service_result_t result = {0};
    rp86_host_protocol_message_t reply = {0};
    if (!rp86_flash_service_handle(dispatch->flash, request, &reply))
        return result;

    result.handled = true;
    result.operation = request->length == 0u ? 0u : request->payload[0];
    result.status = reply.status;
    result.replied = send_reply(dispatch, &reply);
    return result;
}

rp86_host_service_result_t rp86_host_service_dispatch_memory(
    rp86_host_service_dispatch_t *dispatch,
    const rp86_host_protocol_message_t *request) {
    rp86_host_service_result_t result = {0};
    rp86_host_protocol_message_t reply = {0};

    if (dispatch->workload->state == RP86_WORKLOAD_STATE_RUNNING &&
        rp86_shared_mailbox_host_owned(dispatch->workload_memory)) {
        rp86_memory_service_set_write_window(
            dispatch->memory, RP86_SHARED_MAILBOX_BASE,
            RP86_SHARED_MAILBOX_SIZE);
    } else if (dispatch->workload->state == RP86_WORKLOAD_STATE_RUNNING) {
        rp86_memory_service_set_write_window(
            dispatch->memory, RP86_SHARED_MAILBOX_BASE, 0u);
    } else {
        rp86_memory_service_set_write_window(
            dispatch->memory, dispatch->workload_memory->processor_base,
            dispatch->workload_memory->size);
    }
    if (!rp86_memory_service_handle(dispatch->memory, request, &reply))
        return result;

    result.handled = true;
    result.operation = request->length < 12u ? 0u : request->payload[0];
    if (request->length >= 12u) {
        memcpy(&result.address, request->payload + 4u,
               sizeof result.address);
        memcpy(&result.length, request->payload + 8u,
               sizeof result.length);
    }
    result.status = reply.status;
    result.replied = send_reply(dispatch, &reply);
    return result;
}
