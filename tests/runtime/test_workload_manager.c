#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "memory/backing.h"
#include "memory/shared_mailbox.h"
#include "runtime/workload_manager.h"

enum {
    TEST_BASE = 0x20000u,
    TEST_SIZE = 4096u,
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static rp86_workload_manifest_t manifest_for(const uint8_t *image,
                                              size_t length) {
    rp86_workload_manifest_t manifest = {
        .magic = RP86_WORKLOAD_MAGIC,
        .version = RP86_WORKLOAD_FORMAT_VERSION,
        .header_size = sizeof(rp86_workload_manifest_t),
        .image_size = (uint32_t)length,
        .image_crc32 = crc32_update(0u, image, length),
        .load_address = TEST_BASE + 0x20u,
        .entry_segment = (TEST_BASE + 0x20u) >> 4u,
        .entry_offset = 0u,
    };
    return manifest;
}

int main(void) {
    uint8_t storage[TEST_SIZE];
    memset(storage, 0xa5, sizeof storage);

    rp86_memory_backing_t backing;
    rp86_memory_backing_init_direct(&backing, "TEST", TEST_BASE,
                                    storage, sizeof storage);

    rp86_workload_manager_t manager;
    rp86_workload_manager_init(&manager, &backing);

    const uint8_t image[] = {0x90u, 0xb8u, 0x34u, 0x12u, 0xebu, 0xfeu};
    rp86_workload_manifest_t manifest = manifest_for(image, sizeof image);
    assert(rp86_workload_begin(&manager, 7u, &manifest));
    assert(rp86_workload_write(&manager, 7u, 0u, image, 2u));
    assert(rp86_workload_write(&manager, 7u, 2u, image + 2u,
                               sizeof image - 2u));
    assert(rp86_workload_commit(&manager, 7u, manifest.image_crc32));
    assert(manager.state == RP86_WORKLOAD_STATE_STAGED);
    const uint32_t first_workload_id = manager.workload_id;
    assert(!rp86_workload_commit(&manager, 7u, manifest.image_crc32));
    assert(manager.state == RP86_WORKLOAD_STATE_STAGED);
    assert(manager.workload_id == first_workload_id);

    /* load_address, not upload offset zero, selects the physical location. */
    assert(storage[0] == 0xa5u);
    assert(memcmp(storage + 0x20u, image, sizeof image) == 0);

    /* Lifecycle controls are sequence-bound and never invent execution. */
    assert(!rp86_workload_run(&manager, manager.workload_id + 1u));
    assert(rp86_workload_run(&manager, manager.workload_id));
    assert(manager.state == RP86_WORKLOAD_STATE_RUNNING);
    assert(!rp86_workload_commit(&manager, 7u, manifest.image_crc32));
    assert(manager.state == RP86_WORKLOAD_STATE_RUNNING);
    assert(!rp86_workload_run(&manager, manager.workload_id));
    assert(rp86_workload_stop(&manager, 0u));
    assert(manager.state == RP86_WORKLOAD_STATE_STOPPED);
    assert(rp86_workload_restart(&manager, manager.workload_id));
    assert(manager.state == RP86_WORKLOAD_STATE_RUNNING);
    assert(rp86_workload_stop(&manager, manager.workload_id));
    assert(manager.state == RP86_WORKLOAD_STATE_STOPPED);

    /* A manifest outside the selected backing must fail before receiving. */
    rp86_workload_discard(&manager);
    assert(manager.workload_id == 0u);
    assert(manager.manifest.magic == 0u);
    assert(!rp86_workload_run(&manager, first_workload_id));
    manifest.load_address = TEST_BASE + TEST_SIZE - 2u;
    manifest.entry_segment = manifest.load_address >> 4u;
    manifest.entry_offset = manifest.load_address & 0x0fu;
    assert(!rp86_workload_begin(&manager, 8u, &manifest));

    /* Shared memory is also processor-address checked. */
    manifest = manifest_for(image, sizeof image);
    manifest.flags = RP86_WORKLOAD_FLAG_SHARED_MEMORY;
    manifest.shared_base = TEST_BASE + TEST_SIZE;
    manifest.shared_size = 16u;
    assert(!rp86_workload_begin(&manager, 9u, &manifest));

    /* An explicit initial stack must also be backed. */
    manifest = manifest_for(image, sizeof image);
    manifest.stack_segment = 0x0300u;
    manifest.stack_offset = 0u;
    assert(!rp86_workload_begin(&manager, 10u, &manifest));

    /* The fixed Host/processor mailbox is never workload image storage. */
    uint8_t mailbox_region[0x2000u];
    rp86_memory_backing_t mailbox_backing;
    rp86_memory_backing_init_direct(&mailbox_backing, "MAILBOX-TEST",
                                    0x3E000u, mailbox_region,
                                    sizeof mailbox_region);
    rp86_workload_manager_t mailbox_manager;
    rp86_workload_manager_init(&mailbox_manager, &mailbox_backing);
    uint8_t overlapping_image[0x20u] = {0x90u};
    rp86_workload_manifest_t overlapping = {
        .magic = RP86_WORKLOAD_MAGIC,
        .version = RP86_WORKLOAD_FORMAT_VERSION,
        .header_size = sizeof(rp86_workload_manifest_t),
        .image_size = sizeof overlapping_image,
        .image_crc32 = crc32_update(0u, overlapping_image,
                                    sizeof overlapping_image),
        .load_address = RP86_SHARED_MAILBOX_BASE - 0x10u,
        .entry_segment = (RP86_SHARED_MAILBOX_BASE - 0x10u) >> 4u,
        .entry_offset = 0u,
    };
    assert(!rp86_workload_begin(&mailbox_manager, 12u, &overlapping));

    /* CRC failure is visible as a workload fault. */
    manifest = manifest_for(image, sizeof image);
    assert(rp86_workload_begin(&manager, 11u, &manifest));
    assert(rp86_workload_write(&manager, 11u, 0u, image, sizeof image));
    assert(!rp86_workload_commit(&manager, 11u,
                                 manifest.image_crc32 ^ 1u));
    assert(manager.state == RP86_WORKLOAD_STATE_FAULTED);
    return 0;
}
