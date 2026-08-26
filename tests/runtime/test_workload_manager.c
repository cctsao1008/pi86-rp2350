#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "memory/backing.h"
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

static pi86_workload_manifest_t manifest_for(const uint8_t *image,
                                              size_t length) {
    pi86_workload_manifest_t manifest = {
        .magic = PI86_WORKLOAD_MAGIC,
        .version = PI86_WORKLOAD_FORMAT_VERSION,
        .header_size = sizeof(pi86_workload_manifest_t),
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

    pi86_memory_backing_t backing;
    pi86_memory_backing_init_direct(&backing, "TEST", TEST_BASE,
                                    storage, sizeof storage);

    pi86_workload_manager_t manager;
    pi86_workload_manager_init(&manager, &backing);

    const uint8_t image[] = {0x90u, 0xb8u, 0x34u, 0x12u, 0xebu, 0xfeu};
    pi86_workload_manifest_t manifest = manifest_for(image, sizeof image);
    assert(pi86_workload_begin(&manager, 7u, &manifest));
    assert(pi86_workload_write(&manager, 7u, 0u, image, 2u));
    assert(pi86_workload_write(&manager, 7u, 2u, image + 2u,
                               sizeof image - 2u));
    assert(pi86_workload_commit(&manager, 7u, manifest.image_crc32));
    assert(manager.state == PI86_WORKLOAD_STATE_READY);

    /* load_address, not upload offset zero, selects the physical location. */
    assert(storage[0] == 0xa5u);
    assert(memcmp(storage + 0x20u, image, sizeof image) == 0);

    /* A manifest outside the selected backing must fail before receiving. */
    pi86_workload_discard(&manager);
    manifest.load_address = TEST_BASE + TEST_SIZE - 2u;
    manifest.entry_segment = manifest.load_address >> 4u;
    manifest.entry_offset = manifest.load_address & 0x0fu;
    assert(!pi86_workload_begin(&manager, 8u, &manifest));

    /* Shared memory is also processor-address checked. */
    manifest = manifest_for(image, sizeof image);
    manifest.flags = PI86_WORKLOAD_FLAG_SHARED_MEMORY;
    manifest.shared_base = TEST_BASE + TEST_SIZE;
    manifest.shared_size = 16u;
    assert(!pi86_workload_begin(&manager, 9u, &manifest));

    /* An explicit initial stack must also be backed. */
    manifest = manifest_for(image, sizeof image);
    manifest.stack_segment = 0x0300u;
    manifest.stack_offset = 0u;
    assert(!pi86_workload_begin(&manager, 10u, &manifest));

    /* CRC failure is visible as a workload fault. */
    manifest = manifest_for(image, sizeof image);
    assert(pi86_workload_begin(&manager, 11u, &manifest));
    assert(pi86_workload_write(&manager, 11u, 0u, image, sizeof image));
    assert(!pi86_workload_commit(&manager, 11u,
                                 manifest.image_crc32 ^ 1u));
    assert(manager.state == PI86_WORKLOAD_STATE_FAULT);
    return 0;
}
