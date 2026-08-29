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

static bool manifest_valid(const rp86_workload_manifest_t *manifest,
                           const rp86_memory_backing_t *backing) {
    if (manifest == NULL || manifest->magic != RP86_WORKLOAD_MAGIC ||
        manifest->version != RP86_WORKLOAD_FORMAT_VERSION ||
        manifest->header_size != sizeof *manifest ||
        manifest->image_size == 0u || backing == NULL || !backing->available)
        return false;

    const uint32_t image_end = manifest->load_address + manifest->image_size;
    if (manifest->load_address >= RP86_PROCESSOR_ADDRESS_SPACE_SIZE ||
        image_end < manifest->load_address ||
        image_end > RP86_PROCESSOR_ADDRESS_SPACE_SIZE ||
        !rp86_memory_backing_range_valid(backing, manifest->load_address,
                                         manifest->image_size))
        return false;

    const uint32_t entry = ((uint32_t)manifest->entry_segment << 4u) +
                           manifest->entry_offset;
    if (entry < manifest->load_address || entry >= image_end)
        return false;

    /* 0000:0000 means that the workload does not request an initial stack.
     * Any explicit stack must name writable space in the selected backing;
     * validate two bytes because the first PUSH consumes a complete word. */
    if (manifest->stack_segment != 0u || manifest->stack_offset != 0u) {
        const uint32_t stack = ((uint32_t)manifest->stack_segment << 4u) +
                               manifest->stack_offset;
        if (stack >= RP86_PROCESSOR_ADDRESS_SPACE_SIZE ||
            !rp86_memory_backing_range_valid(backing, stack, 2u))
            return false;
    }

    const uint32_t known_flags = RP86_WORKLOAD_FLAG_PERSISTENT |
                                 RP86_WORKLOAD_FLAG_STDIO |
                                 RP86_WORKLOAD_FLAG_SHARED_MEMORY |
                                 RP86_WORKLOAD_FLAG_CLOCK_FREE_RUNNING |
                                 RP86_WORKLOAD_FLAG_CLOCK_STEPPED;
    if ((manifest->flags & ~known_flags) != 0u) return false;
    if ((manifest->flags & RP86_WORKLOAD_FLAG_CLOCK_FREE_RUNNING) != 0u &&
        (manifest->flags & RP86_WORKLOAD_FLAG_CLOCK_STEPPED) != 0u)
        return false;
    if ((manifest->shared_size == 0u) !=
        ((manifest->flags & RP86_WORKLOAD_FLAG_SHARED_MEMORY) == 0u))
        return false;
    if (manifest->shared_size != 0u) {
        const uint32_t shared_end = manifest->shared_base + manifest->shared_size;
        if (shared_end < manifest->shared_base ||
            shared_end > RP86_PROCESSOR_ADDRESS_SPACE_SIZE ||
            !rp86_memory_backing_range_valid(backing, manifest->shared_base,
                                             manifest->shared_size))
            return false;
    }
    return true;
}

void rp86_workload_manager_init(rp86_workload_manager_t *manager,
                                rp86_memory_backing_t *backing) {
    memset(manager, 0, sizeof *manager);
    manager->backing = backing;
    manager->state = RP86_WORKLOAD_STATE_EMPTY;
}

bool rp86_workload_begin(rp86_workload_manager_t *manager,
                         uint32_t transfer_id,
                         const rp86_workload_manifest_t *manifest) {
    if (manager == NULL || manager->backing == NULL ||
        !manager->backing->available ||
        !manifest_valid(manifest, manager->backing) ||
        manager->state == RP86_WORKLOAD_STATE_RUNNING)
        return false;

    manager->manifest = *manifest;
    manager->transfer_id = transfer_id;
    manager->received = 0u;
    manager->running_crc32 = 0u;
    manager->state = RP86_WORKLOAD_STATE_RECEIVING;
    return true;
}

bool rp86_workload_write(rp86_workload_manager_t *manager,
                         uint32_t transfer_id, uint32_t offset,
                         const uint8_t *data, size_t length) {
    if (manager == NULL || manager->state != RP86_WORKLOAD_STATE_RECEIVING ||
        transfer_id != manager->transfer_id || offset != manager->received ||
        length == 0u || data == NULL ||
        length > manager->manifest.image_size - manager->received)
        return false;

    const uint32_t address = manager->manifest.load_address + offset;
    if (!rp86_memory_backing_write(manager->backing, address, data, length))
        return false;
    manager->running_crc32 = crc32_update(manager->running_crc32, data, length);
    manager->received += (uint32_t)length;
    return true;
}

bool rp86_workload_commit(rp86_workload_manager_t *manager,
                          uint32_t transfer_id, uint32_t expected_crc32) {
    if (manager == NULL || manager->state != RP86_WORKLOAD_STATE_RECEIVING)
        return false;

    if (transfer_id != manager->transfer_id ||
        manager->received != manager->manifest.image_size ||
        expected_crc32 != manager->manifest.image_crc32 ||
        manager->running_crc32 != expected_crc32) {
        manager->state = RP86_WORKLOAD_STATE_FAULTED;
        return false;
    }

    rp86_memory_backing_publish(manager->backing);
    manager->workload_id++;
    if (manager->workload_id == 0u) manager->workload_id = 1u;
    manager->state = RP86_WORKLOAD_STATE_STAGED;
    return true;
}

static bool workload_id_matches(const rp86_workload_manager_t *manager,
                                uint32_t workload_id) {
    return manager != NULL && manager->workload_id != 0u &&
        (workload_id == 0u || workload_id == manager->workload_id);
}

bool rp86_workload_run(rp86_workload_manager_t *manager,
                       uint32_t workload_id) {
    if (!workload_id_matches(manager, workload_id) ||
        (manager->state != RP86_WORKLOAD_STATE_STAGED &&
         manager->state != RP86_WORKLOAD_STATE_STOPPED))
        return false;
    manager->state = RP86_WORKLOAD_STATE_RUNNING;
    return true;
}

bool rp86_workload_stop(rp86_workload_manager_t *manager,
                        uint32_t workload_id) {
    if (!workload_id_matches(manager, workload_id) ||
        manager->state != RP86_WORKLOAD_STATE_RUNNING)
        return false;
    manager->state = RP86_WORKLOAD_STATE_STOPPED;
    return true;
}

bool rp86_workload_restart(rp86_workload_manager_t *manager,
                           uint32_t workload_id) {
    if (!workload_id_matches(manager, workload_id) ||
        (manager->state != RP86_WORKLOAD_STATE_STAGED &&
         manager->state != RP86_WORKLOAD_STATE_RUNNING &&
         manager->state != RP86_WORKLOAD_STATE_STOPPED))
        return false;
    manager->state = RP86_WORKLOAD_STATE_RUNNING;
    return true;
}

void rp86_workload_discard(rp86_workload_manager_t *manager) {
    if (manager == NULL) return;
    memset(&manager->manifest, 0, sizeof manager->manifest);
    manager->transfer_id = 0u;
    manager->workload_id = 0u;
    manager->received = 0u;
    manager->running_crc32 = 0u;
    manager->state = RP86_WORKLOAD_STATE_EMPTY;
}

const char *rp86_workload_state_name(rp86_workload_state_t state) {
    switch (state) {
        case RP86_WORKLOAD_STATE_EMPTY: return "EMPTY";
        case RP86_WORKLOAD_STATE_RECEIVING: return "RECEIVING";
        case RP86_WORKLOAD_STATE_STAGED: return "STAGED";
        case RP86_WORKLOAD_STATE_RUNNING: return "RUNNING";
        case RP86_WORKLOAD_STATE_STOPPED: return "STOPPED";
        case RP86_WORKLOAD_STATE_EXITED: return "EXITED";
        case RP86_WORKLOAD_STATE_FAULTED: return "FAULTED";
        case RP86_WORKLOAD_STATE_TIMED_OUT: return "TIMED_OUT";
        default: return "UNKNOWN";
    }
}
