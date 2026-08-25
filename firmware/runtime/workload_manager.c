#include "runtime/workload_manager.h"

#include <string.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool manifest_valid(const pi86_workload_manifest_t *manifest,
                           size_t backing_size) {
    if (manifest == NULL || manifest->magic != PI86_WORKLOAD_MAGIC ||
        manifest->version != PI86_WORKLOAD_FORMAT_VERSION ||
        manifest->header_size != sizeof *manifest ||
        manifest->image_size == 0u || manifest->image_size > backing_size)
        return false;

    const uint32_t image_end = manifest->load_address + manifest->image_size;
    if (manifest->load_address >= PI86_V30_ADDRESS_SPACE_SIZE ||
        image_end < manifest->load_address ||
        image_end > PI86_V30_ADDRESS_SPACE_SIZE)
        return false;

    const uint32_t entry = ((uint32_t)manifest->entry_segment << 4u) +
                           manifest->entry_offset;
    if (entry < manifest->load_address || entry >= image_end)
        return false;

    const uint32_t known_flags = PI86_WORKLOAD_FLAG_PERSISTENT |
                                 PI86_WORKLOAD_FLAG_STDIO |
                                 PI86_WORKLOAD_FLAG_SHARED_MEMORY;
    if ((manifest->flags & ~known_flags) != 0u) return false;
    if ((manifest->shared_size == 0u) !=
        ((manifest->flags & PI86_WORKLOAD_FLAG_SHARED_MEMORY) == 0u))
        return false;
    if (manifest->shared_size != 0u) {
        const uint32_t shared_end = manifest->shared_base + manifest->shared_size;
        if (shared_end < manifest->shared_base ||
            shared_end > PI86_V30_ADDRESS_SPACE_SIZE)
            return false;
    }
    return true;
}

void pi86_workload_manager_init(pi86_workload_manager_t *manager,
                                pi86_psram_backing_t *backing) {
    memset(manager, 0, sizeof *manager);
    manager->backing = backing;
    manager->state = PI86_WORKLOAD_STATE_EMPTY;
}

bool pi86_workload_begin(pi86_workload_manager_t *manager,
                         uint32_t transfer_id,
                         const pi86_workload_manifest_t *manifest) {
    if (manager == NULL || manager->backing == NULL ||
        !manager->backing->available ||
        !manifest_valid(manifest, manager->backing->size) ||
        manager->state == PI86_WORKLOAD_STATE_RUNNING)
        return false;

    manager->manifest = *manifest;
    manager->transfer_id = transfer_id;
    manager->received = 0u;
    manager->running_crc32 = 0u;
    manager->state = PI86_WORKLOAD_STATE_RECEIVING;
    return true;
}

bool pi86_workload_write(pi86_workload_manager_t *manager,
                         uint32_t transfer_id, uint32_t offset,
                         const uint8_t *data, size_t length) {
    if (manager == NULL || manager->state != PI86_WORKLOAD_STATE_RECEIVING ||
        transfer_id != manager->transfer_id || offset != manager->received ||
        length == 0u || data == NULL ||
        length > manager->manifest.image_size - manager->received)
        return false;

    if (!pi86_psram_write(manager->backing, offset, data, length)) return false;
    manager->running_crc32 = crc32_update(manager->running_crc32, data, length);
    manager->received += (uint32_t)length;
    return true;
}

bool pi86_workload_commit(pi86_workload_manager_t *manager,
                          uint32_t transfer_id, uint32_t expected_crc32) {
    if (manager == NULL || manager->state != PI86_WORKLOAD_STATE_RECEIVING ||
        transfer_id != manager->transfer_id ||
        manager->received != manager->manifest.image_size ||
        expected_crc32 != manager->manifest.image_crc32 ||
        manager->running_crc32 != expected_crc32) {
        if (manager != NULL) manager->state = PI86_WORKLOAD_STATE_FAULT;
        return false;
    }

    pi86_psram_publish();
    manager->workload_id++;
    if (manager->workload_id == 0u) manager->workload_id = 1u;
    manager->state = PI86_WORKLOAD_STATE_READY;
    return true;
}

void pi86_workload_discard(pi86_workload_manager_t *manager) {
    if (manager == NULL) return;
    manager->transfer_id = 0u;
    manager->received = 0u;
    manager->running_crc32 = 0u;
    manager->state = PI86_WORKLOAD_STATE_EMPTY;
}

const char *pi86_workload_state_name(pi86_workload_state_t state) {
    switch (state) {
        case PI86_WORKLOAD_STATE_EMPTY: return "EMPTY";
        case PI86_WORKLOAD_STATE_RECEIVING: return "RECEIVING";
        case PI86_WORKLOAD_STATE_READY: return "READY";
        case PI86_WORKLOAD_STATE_RUNNING: return "RUNNING";
        case PI86_WORKLOAD_STATE_STOPPED: return "STOPPED";
        case PI86_WORKLOAD_STATE_EXITED: return "EXITED";
        case PI86_WORKLOAD_STATE_FAULT: return "FAULT";
        case PI86_WORKLOAD_STATE_TIMEOUT: return "TIMEOUT";
        default: return "UNKNOWN";
    }
}
