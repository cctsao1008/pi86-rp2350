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

_Static_assert(MEMORY_PAYLOAD_HEADER_SIZE + RP86_MEMORY_DATA_BYTES <=
                   RP86_HOST_PROTOCOL_PAYLOAD_SIZE,
               "memory result exceeds Host Protocol payload");

static bool write_range_allowed(const rp86_memory_service_t *service,
                                uint32_t address, size_t length) {
    if (address < service->write_base) return false;
    const size_t offset = (size_t)(address - service->write_base);
    return offset <= service->write_size &&
           length <= service->write_size - offset;
}

void rp86_memory_service_init(rp86_memory_service_t *service,
                              rp86_memory_backing_t *backing) {
    service->backing = backing;
    service->write_base = backing == NULL ? 0u : backing->processor_base;
    service->write_size = backing == NULL ? 0u : backing->size;
}

void rp86_memory_service_set_write_window(rp86_memory_service_t *service,
                                          uint32_t processor_base,
                                          size_t size) {
    if (service == NULL) return;
    service->write_base = processor_base;
    service->write_size = size;
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
        if (!write_range_allowed(service, header.address, header.length)) {
            reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_STATE;
            return true;
        }
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
