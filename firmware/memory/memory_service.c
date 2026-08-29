#include "memory/memory_service.h"

#include <string.h>

typedef struct __attribute__((packed)) {
    uint8_t operation;
    uint8_t reserved[3];
    uint32_t address;
    uint32_t length;
    uint8_t data[];
} memory_payload_t;

enum { MEMORY_PAYLOAD_HEADER_SIZE = 12u };

void rp86_memory_service_init(rp86_memory_service_t *service,
                              rp86_memory_backing_t *backing) {
    service->backing = backing;
}

bool rp86_memory_service_handle(
    rp86_memory_service_t *service,
    const rp86_host_protocol_message_t *request,
    rp86_host_protocol_message_t *reply) {
    if (service == NULL || request == NULL || reply == NULL) return false;
    memset(reply, 0, sizeof *reply);
    reply->version = RP86_HOST_PROTOCOL_VERSION;
    reply->type = RP86_HOST_PROTOCOL_MESSAGE_MEMORY_RESULT;
    reply->sequence = request->sequence;

    if (request->version != RP86_HOST_PROTOCOL_VERSION ||
        request->type != RP86_HOST_PROTOCOL_MESSAGE_MEMORY_REQUEST ||
        request->status != RP86_HOST_PROTOCOL_STATUS_OK ||
        request->length < MEMORY_PAYLOAD_HEADER_SIZE) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH;
        return true;
    }

    memory_payload_t header;
    memcpy(&header, request->payload, MEMORY_PAYLOAD_HEADER_SIZE);
    memcpy(reply->payload, &header, MEMORY_PAYLOAD_HEADER_SIZE);
    reply->length = MEMORY_PAYLOAD_HEADER_SIZE;
    if (service->backing == NULL || !service->backing->available) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE;
        return true;
    }
    if (header.length > RP86_MEMORY_DATA_BYTES ||
        !rp86_memory_backing_range_valid(service->backing, header.address,
                                         header.length)) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH;
        return true;
    }

    if (header.operation == RP86_MEMORY_READ &&
        request->length == MEMORY_PAYLOAD_HEADER_SIZE) {
        if (!rp86_memory_backing_read(service->backing, header.address,
                                      reply->payload + MEMORY_PAYLOAD_HEADER_SIZE,
                                      header.length)) {
            reply->status = RP86_HOST_PROTOCOL_STATUS_IO_ERROR;
            return true;
        }
        reply->length = MEMORY_PAYLOAD_HEADER_SIZE + header.length;
        reply->status = RP86_HOST_PROTOCOL_STATUS_OK;
        return true;
    }
    if (header.operation == RP86_MEMORY_WRITE &&
        request->length == MEMORY_PAYLOAD_HEADER_SIZE + header.length) {
        if (!rp86_memory_backing_write(
                service->backing, header.address,
                request->payload + MEMORY_PAYLOAD_HEADER_SIZE,
                header.length)) {
            reply->status = RP86_HOST_PROTOCOL_STATUS_IO_ERROR;
            return true;
        }
        rp86_memory_backing_publish(service->backing);
        reply->status = RP86_HOST_PROTOCOL_STATUS_OK;
        return true;
    }

    reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH;
    return true;
}
