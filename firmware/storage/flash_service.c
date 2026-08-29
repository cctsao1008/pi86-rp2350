#include "storage/flash_service.h"

#include <string.h>

#include "diskio.h"
#include "storage/flash_disk.h"
#include "storage/flash_layout.h"

#define RP86_UPLOAD_TEMP_PATH "flash:/PI86UPLD.TMP"

static uint16_t load_u16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t load_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void store_u32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data,
                             uint32_t length) {
    while (length-- != 0u) {
        crc ^= *data++;
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static uint16_t status_from_fatfs(FRESULT result) {
    switch (result) {
    case FR_OK: return RP86_HOST_PROTOCOL_STATUS_OK;
    case FR_NO_FILE:
    case FR_NO_PATH: return RP86_HOST_PROTOCOL_STATUS_NOT_FOUND;
    case FR_INVALID_NAME:
    case FR_INVALID_PARAMETER: return RP86_HOST_PROTOCOL_STATUS_INVALID_PATH;
    case FR_DENIED:
    case FR_EXIST: return RP86_HOST_PROTOCOL_STATUS_NO_SPACE;
    case FR_NOT_READY:
    case FR_INVALID_DRIVE:
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM: return RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE;
    default: return RP86_HOST_PROTOCOL_STATUS_IO_ERROR;
    }
}

static bool valid_flash_path(const uint8_t *source, uint8_t length,
                             char *destination, size_t capacity) {
    if (length < 6u || (size_t)length >= capacity) return false;
    memcpy(destination, source, length);
    destination[length] = '\0';
    if (memcmp(destination, "flash:", 6u) != 0) return false;
    if (length > 6u && destination[6] != '/') return false;
    if (strstr(destination, "\\") != NULL) return false;
    const char *segment = destination + 6u;
    while (*segment != '\0') {
        while (*segment == '/') ++segment;
        const char *end = segment;
        while (*end != '\0' && *end != '/') ++end;
        if ((end - segment == 1 && segment[0] == '.') ||
            (end - segment == 2 && segment[0] == '.' && segment[1] == '.'))
            return false;
        for (const char *cursor = segment; cursor < end; ++cursor)
            if ((unsigned char)*cursor < 0x20u) return false;
        segment = end;
    }
    return strcmp(destination, RP86_UPLOAD_TEMP_PATH) != 0;
}

static void prepare_reply(const rp86_host_protocol_message_t *request,
                          rp86_host_protocol_message_t *reply, uint8_t operation) {
    memset(reply, 0, sizeof *reply);
    reply->version = RP86_HOST_PROTOCOL_VERSION;
    reply->type = RP86_HOST_PROTOCOL_MESSAGE_FILESYSTEM_RESULT;
    reply->sequence = request->sequence;
    reply->status = RP86_HOST_PROTOCOL_STATUS_OK;
    reply->payload[0] = operation;
}

static void abort_upload(rp86_flash_service_t *service) {
    if (service->upload_open) (void)f_close(&service->upload);
    service->upload_open = false;
    service->transfer_id = 0u;
    service->expected_size = 0u;
    service->received_size = 0u;
    service->running_crc32 = 0xffffffffu;
    (void)f_unlink(RP86_UPLOAD_TEMP_PATH);
}

static bool handle_list(rp86_flash_service_t *service,
                        const rp86_host_protocol_message_t *request,
                        rp86_host_protocol_message_t *reply) {
    (void)service;
    if (request->length < 4u) return false;
    const uint8_t path_length = request->payload[1];
    if (request->length != (uint16_t)(4u + path_length)) return false;
    char path[49];
    if (!valid_flash_path(request->payload + 4u, path_length,
                          path, sizeof path)) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_INVALID_PATH;
        return true;
    }

    DIR directory;
    FRESULT result = f_opendir(&directory, path);
    if (result != FR_OK) {
        reply->status = status_from_fatfs(result);
        return true;
    }

    const uint16_t wanted = load_u16(request->payload + 2u);
    uint16_t visible = 0u;
    FILINFO info;
    memset(&info, 0, sizeof info);
    while (true) {
        result = f_readdir(&directory, &info);
        if (result != FR_OK || info.fname[0] == '\0') break;
        if (strcmp(info.fname, "PI86UPLD.TMP") == 0) continue;
        if (visible++ == wanted) break;
    }
    const FRESULT close_result = f_closedir(&directory);
    if (result == FR_OK) result = close_result;
    if (result != FR_OK) {
        reply->status = status_from_fatfs(result);
        return true;
    }

    reply->length = RP86_HOST_PROTOCOL_PAYLOAD_SIZE;
    if (info.fname[0] == '\0') {
        reply->payload[1] = RP86_FILESYSTEM_FLAG_EOF;
        return true;
    }

    size_t name_length = strlen(info.fname);
    if (name_length > RP86_FILESYSTEM_LIST_NAME_BYTES) {
        name_length = RP86_FILESYSTEM_LIST_NAME_BYTES;
        reply->payload[1] |= RP86_FILESYSTEM_FLAG_TRUNCATED;
    }
    if ((info.fattrib & AM_DIR) != 0u)
        reply->payload[1] |= RP86_FILESYSTEM_FLAG_DIRECTORY;
    reply->payload[2] = info.fattrib;
    reply->payload[3] = (uint8_t)name_length;
    store_u16(reply->payload + 4u, (uint16_t)(wanted + 1u));
    store_u32(reply->payload + 6u, (uint32_t)info.fsize);
    memcpy(reply->payload + 10u, info.fname, name_length);
    return true;
}

static bool handle_df(rp86_flash_service_t *service,
                      const rp86_host_protocol_message_t *request,
                      rp86_host_protocol_message_t *reply) {
    if (request->length < 2u) return false;
    const uint8_t path_length = request->payload[1];
    if (request->length != (uint16_t)(2u + path_length)) return false;
    char path[51];
    if (!valid_flash_path(request->payload + 2u, path_length,
                          path, sizeof path)) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_INVALID_PATH;
        return true;
    }
    DWORD free_clusters = 0u;
    FATFS *filesystem = NULL;
    const FRESULT result = f_getfree(path, &free_clusters, &filesystem);
    if (result != FR_OK || filesystem == NULL) {
        reply->status = status_from_fatfs(result);
        return true;
    }
    const uint32_t total_kib = (uint32_t)(((uint64_t)
        (filesystem->n_fatent - 2u) * filesystem->csize *
        RP86_FLASH_SECTOR_BYTES) / 1024u);
    const uint32_t free_kib = (uint32_t)(((uint64_t)free_clusters *
        filesystem->csize * RP86_FLASH_SECTOR_BYTES) / 1024u);
    service->volume->free_kib = free_kib;
    size_t label_length = strlen(service->volume->label);
    if (label_length > 32u) label_length = 32u;
    reply->payload[1] = filesystem->fs_type;
    reply->payload[2] = (uint8_t)label_length;
    store_u32(reply->payload + 4u, total_kib);
    store_u32(reply->payload + 8u, free_kib);
    store_u32(reply->payload + 12u,
              filesystem->csize * RP86_FLASH_SECTOR_BYTES);
    store_u32(reply->payload + 16u, RP86_FLASH_ERASE_BYTES);
    memcpy(reply->payload + 20u, service->volume->label, label_length);
    reply->length = RP86_HOST_PROTOCOL_PAYLOAD_SIZE;
    return true;
}

static bool handle_read(const rp86_host_protocol_message_t *request,
                        rp86_host_protocol_message_t *reply) {
    if (request->length < 8u) return false;
    const uint8_t path_length = request->payload[1];
    if (request->length != (uint16_t)(8u + path_length)) return false;
    char path[RP86_FILESYSTEM_READ_PATH_BYTES + 1u];
    if (!valid_flash_path(request->payload + 8u, path_length,
                          path, sizeof path)) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_INVALID_PATH;
        return true;
    }
    FIL file;
    FRESULT result = f_open(&file, path, FA_READ);
    if (result != FR_OK) {
        reply->status = status_from_fatfs(result);
        return true;
    }
    const uint32_t offset = load_u32(request->payload + 4u);
    const uint32_t size = (uint32_t)f_size(&file);
    if (offset <= size) result = f_lseek(&file, offset);
    else result = FR_INVALID_PARAMETER;
    UINT transferred = 0u;
    if (result == FR_OK)
        result = f_read(&file, reply->payload + 12u,
                        RP86_FILESYSTEM_READ_DATA_BYTES, &transferred);
    const FRESULT close_result = f_close(&file);
    if (result == FR_OK) result = close_result;
    if (result != FR_OK) {
        reply->status = status_from_fatfs(result);
        return true;
    }
    if (offset + transferred >= size)
        reply->payload[1] |= RP86_FILESYSTEM_FLAG_EOF;
    store_u16(reply->payload + 2u, (uint16_t)transferred);
    store_u32(reply->payload + 4u, offset);
    store_u32(reply->payload + 8u, size);
    reply->length = (uint16_t)(12u + transferred);
    return true;
}

static bool handle_write_begin(rp86_flash_service_t *service,
                               const rp86_host_protocol_message_t *request,
                               rp86_host_protocol_message_t *reply) {
    if (request->length < 16u) return false;
    const uint8_t path_length = request->payload[1];
    if (request->length != (uint16_t)(16u + path_length)) return false;
    char path[RP86_FILESYSTEM_WRITE_PATH_BYTES + 1u];
    if (!valid_flash_path(request->payload + 16u, path_length,
                          path, sizeof path)) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_INVALID_PATH;
        return true;
    }
    abort_upload(service);
    FRESULT result = f_open(&service->upload, RP86_UPLOAD_TEMP_PATH,
                            FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        reply->status = status_from_fatfs(result);
        return true;
    }
    service->upload_open = true;
    service->transfer_id = load_u32(request->payload + 4u);
    service->expected_size = load_u32(request->payload + 8u);
    service->expected_crc32 = load_u32(request->payload + 12u);
    service->received_size = 0u;
    service->running_crc32 = 0xffffffffu;
    memcpy(service->target_path, path, path_length + 1u);
    store_u32(reply->payload + 4u, service->transfer_id);
    reply->length = 8u;
    return true;
}

static bool handle_write_data(rp86_flash_service_t *service,
                              const rp86_host_protocol_message_t *request,
                              rp86_host_protocol_message_t *reply) {
    if (request->length < 12u) return false;
    const uint16_t data_length = load_u16(request->payload + 2u);
    if (data_length > RP86_FILESYSTEM_WRITE_DATA_BYTES ||
        request->length != (uint16_t)(12u + data_length)) return false;
    const uint32_t transfer = load_u32(request->payload + 4u);
    const uint32_t offset = load_u32(request->payload + 8u);
    if (!service->upload_open || transfer != service->transfer_id ||
        offset != service->received_size ||
        offset + data_length > service->expected_size) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_STATE;
        return true;
    }
    UINT transferred = 0u;
    const FRESULT result = f_write(&service->upload,
                                   request->payload + 12u,
                                   data_length, &transferred);
    if (result != FR_OK || transferred != data_length) {
        reply->status = result == FR_OK ? RP86_HOST_PROTOCOL_STATUS_NO_SPACE :
                                         status_from_fatfs(result);
        abort_upload(service);
        return true;
    }
    service->running_crc32 = crc32_update(service->running_crc32,
                                          request->payload + 12u,
                                          data_length);
    service->received_size += data_length;
    store_u32(reply->payload + 4u, transfer);
    store_u32(reply->payload + 8u, service->received_size);
    reply->length = 12u;
    return true;
}

static bool handle_write_commit(rp86_flash_service_t *service,
                                const rp86_host_protocol_message_t *request,
                                rp86_host_protocol_message_t *reply) {
    if (request->length != 8u) return false;
    const uint32_t transfer = load_u32(request->payload + 4u);
    const uint32_t crc = service->running_crc32 ^ 0xffffffffu;
    if (!service->upload_open || transfer != service->transfer_id ||
        service->received_size != service->expected_size) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_STATE;
        return true;
    }
    if (crc != service->expected_crc32) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_CRC;
        abort_upload(service);
        return true;
    }
    FRESULT result = f_sync(&service->upload);
    const FRESULT close_result = f_close(&service->upload);
    service->upload_open = false;
    if (result == FR_OK) result = close_result;
    if (result == FR_OK && !rp86_flash_disk_flush()) result = FR_DISK_ERR;
    if (result == FR_OK) {
        const FRESULT unlink_result = f_unlink(service->target_path);
        if (unlink_result != FR_OK && unlink_result != FR_NO_FILE &&
            unlink_result != FR_NO_PATH)
            result = unlink_result;
    }
    if (result == FR_OK)
        result = f_rename(RP86_UPLOAD_TEMP_PATH, service->target_path);
    if (result == FR_OK && !rp86_flash_disk_flush()) result = FR_DISK_ERR;
    if (result != FR_OK) {
        reply->status = status_from_fatfs(result);
        (void)f_unlink(RP86_UPLOAD_TEMP_PATH);
        return true;
    }
    store_u32(reply->payload + 4u, transfer);
    store_u32(reply->payload + 8u, service->received_size);
    store_u32(reply->payload + 12u, crc);
    reply->length = 16u;
    service->transfer_id = 0u;
    return true;
}

void rp86_flash_service_init(rp86_flash_service_t *service,
                             rp86_flash_volume_t *volume, bool available) {
    memset(service, 0, sizeof *service);
    service->volume = volume;
    service->available = available;
    service->running_crc32 = 0xffffffffu;
    if (available) (void)f_unlink(RP86_UPLOAD_TEMP_PATH);
}

bool rp86_flash_service_handle(rp86_flash_service_t *service,
                               const rp86_host_protocol_message_t *request,
                               rp86_host_protocol_message_t *reply) {
    if (request->version != RP86_HOST_PROTOCOL_VERSION ||
        request->type != RP86_HOST_PROTOCOL_MESSAGE_FILESYSTEM_REQUEST ||
        request->status != RP86_HOST_PROTOCOL_STATUS_OK || request->length == 0u)
        return false;
    const uint8_t operation = request->payload[0];
    prepare_reply(request, reply, operation);
    if (!service->available) {
        reply->status = RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE;
        return true;
    }
    bool valid = false;
    switch (operation) {
    case RP86_FILESYSTEM_LIST:
        valid = handle_list(service, request, reply);
        break;
    case RP86_FILESYSTEM_DF:
        valid = handle_df(service, request, reply);
        break;
    case RP86_FILESYSTEM_READ:
        valid = handle_read(request, reply);
        break;
    case RP86_FILESYSTEM_WRITE_BEGIN:
        valid = handle_write_begin(service, request, reply);
        break;
    case RP86_FILESYSTEM_WRITE_DATA:
        valid = handle_write_data(service, request, reply);
        break;
    case RP86_FILESYSTEM_WRITE_COMMIT:
        valid = handle_write_commit(service, request, reply);
        break;
    default:
        reply->status = RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE;
        return true;
    }
    if (!valid) reply->status = RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH;
    return true;
}
