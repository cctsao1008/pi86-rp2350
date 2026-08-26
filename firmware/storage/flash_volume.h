#ifndef PI86_FLASH_VOLUME_H
#define PI86_FLASH_VOLUME_H

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
} pi86_flash_volume_t;

bool pi86_flash_volume_init(pi86_flash_volume_t *volume);
const char *pi86_flash_filesystem_name(BYTE filesystem_type);

#endif
