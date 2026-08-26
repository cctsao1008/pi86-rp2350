#ifndef PI86_FLASH_SERVICE_H
#define PI86_FLASH_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "ai_bridge/bridge_protocol.h"
#include "ff.h"
#include "storage/flash_volume.h"

typedef struct {
    pi86_flash_volume_t *volume;
    FIL upload;
    bool available;
    bool upload_open;
    uint32_t transfer_id;
    uint32_t expected_size;
    uint32_t expected_crc32;
    uint32_t received_size;
    uint32_t running_crc32;
    char target_path[PI86_FILESYSTEM_WRITE_PATH_BYTES + 1u];
} pi86_flash_service_t;

void pi86_flash_service_init(pi86_flash_service_t *service,
                             pi86_flash_volume_t *volume, bool available);
bool pi86_flash_service_handle(pi86_flash_service_t *service,
                               const pi86_bridge_message_t *request,
                               pi86_bridge_message_t *reply);

#endif
