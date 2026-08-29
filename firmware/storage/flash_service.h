#ifndef RP86_FLASH_SERVICE_H
#define RP86_FLASH_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "host_protocol/host_protocol.h"
#include "ff.h"
#include "storage/flash_volume.h"

typedef struct {
    rp86_flash_volume_t *volume;
    FIL upload;
    bool available;
    bool upload_open;
    uint32_t transfer_id;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t received_size;
    uint32_t running_crc32;
    char target_path[RP86_FILESYSTEM_WRITE_PATH_BYTES + 1u];
} rp86_flash_service_t;

void rp86_flash_service_init(rp86_flash_service_t *service,
                             rp86_flash_volume_t *volume, bool available);
bool rp86_flash_service_handle(rp86_flash_service_t *service,
                               const rp86_host_protocol_message_t *request,
                               rp86_host_protocol_message_t *reply);

#endif
