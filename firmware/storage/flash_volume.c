#include "storage/flash_volume.h"

#include <string.h>

#include "diskio.h"
#include "storage/flash_disk.h"
#include "storage/flash_layout.h"

#define PI86_FLASH_PATH "flash:"
#define PI86_FLASH_TEST_PATH "flash:/PI86TEST.TMP"
#define RP_FLASH_LABEL "RP-FLASH"

static BYTE g_format_work[PI86_FLASH_ERASE_BYTES];

static bool run_first_format_self_test(void) {
    static const char payload[] = "pi86-rp2350 flash self-test\n";
    char readback[sizeof payload] = {0};
    FIL file;
    UINT transferred = 0u;

    FRESULT result = f_open(&file, PI86_FLASH_TEST_PATH,
                            FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) return false;
    result = f_write(&file, payload, sizeof payload, &transferred);
    if (result == FR_OK && transferred == sizeof payload)
        result = f_sync(&file);
    const FRESULT close_write = f_close(&file);
    if (result == FR_OK) result = close_write;
    if (result != FR_OK || !pi86_flash_disk_flush()) return false;

    result = f_open(&file, PI86_FLASH_TEST_PATH, FA_READ);
    if (result != FR_OK) return false;
    transferred = 0u;
    result = f_read(&file, readback, sizeof readback, &transferred);
    const FRESULT close_read = f_close(&file);
    if (result == FR_OK) result = close_read;
    const bool matches = result == FR_OK && transferred == sizeof payload &&
                         memcmp(payload, readback, sizeof payload) == 0;
    const FRESULT remove_result = f_unlink(PI86_FLASH_TEST_PATH);
    return matches && remove_result == FR_OK && pi86_flash_disk_flush();
}

const char *pi86_flash_filesystem_name(BYTE filesystem_type) {
    switch (filesystem_type) {
    case FS_FAT12: return "FAT12";
    case FS_FAT16: return "FAT16";
    case FS_FAT32: return "FAT32";
    default: return "UNKNOWN";
    }
}

bool pi86_flash_volume_init(pi86_flash_volume_t *volume) {
    memset(volume, 0, sizeof *volume);
    volume->result = FR_NOT_READY;
    if (disk_initialize(0u) & STA_NOINIT) return false;

    volume->result = f_mount(&volume->filesystem, PI86_FLASH_PATH, 1u);
    if (volume->result == FR_NO_FILESYSTEM) {
        const MKFS_PARM options = {
            .fmt = FM_FAT | FM_SFD,
            .n_fat = 2u,
            .align = PI86_FLASH_ERASE_SECTORS,
            .n_root = 512u,
            .au_size = PI86_FLASH_CLUSTER_BYTES,
        };
        volume->result = f_mkfs(PI86_FLASH_PATH, &options,
                                g_format_work, sizeof g_format_work);
        if (volume->result != FR_OK || !pi86_flash_disk_flush()) return false;
        volume->formatted_on_boot = true;

        volume->result = f_mount(&volume->filesystem, PI86_FLASH_PATH, 1u);
        if (volume->result != FR_OK) return false;
        volume->self_test_passed = run_first_format_self_test();
        if (!volume->self_test_passed) {
            volume->result = FR_DISK_ERR;
            return false;
        }
    } else if (volume->result != FR_OK) {
        return false;
    } else {
        volume->self_test_passed = true;
    }

    DWORD serial = 0u;
    volume->result = f_getlabel(PI86_FLASH_PATH, volume->label, &serial);
    if (volume->result != FR_OK) return false;
    if (strcmp(volume->label, RP_FLASH_LABEL) != 0) {
        volume->result = f_setlabel("flash:RP-FLASH");
        if (volume->result != FR_OK || !pi86_flash_disk_flush()) return false;
        volume->result = f_getlabel(PI86_FLASH_PATH, volume->label, &serial);
        if (volume->result != FR_OK) return false;
    }

    DWORD free_clusters = 0u;
    FATFS *filesystem = NULL;
    volume->result = f_getfree(PI86_FLASH_PATH, &free_clusters, &filesystem);
    if (volume->result != FR_OK || filesystem == NULL) return false;
    volume->free_kib = (uint32_t)(((uint64_t)free_clusters *
                                  filesystem->csize *
                                  PI86_FLASH_SECTOR_BYTES) / 1024u);
    volume->filesystem_type = filesystem->fs_type;
    volume->mounted = pi86_flash_disk_healthy();
    return volume->mounted;
}
