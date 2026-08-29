#ifndef RP86_FLASH_VOLUME_H
#define RP86_FLASH_VOLUME_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

typedef struct {
    FATFS filesystem;
    FRESULT result;
    bool mounted;
    bool formatted_on_boot;
    bool self_test_passed;
    BYTE filesystem_type;
    uint32_t free_kib;
    char label[12];
} rp86_flash_volume_t;

bool rp86_flash_volume_init(rp86_flash_volume_t *volume);
const char *rp86_flash_filesystem_name(BYTE filesystem_type);

#endif
