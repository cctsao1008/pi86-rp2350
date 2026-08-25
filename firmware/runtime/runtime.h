#ifndef PI86_RUNTIME_H
#define PI86_RUNTIME_H

#include <stddef.h>

#include "board/rp2350_pizero_resources.h"
#include "memory/psram_backing.h"
#include "runtime/workload_manager.h"

typedef enum {
    PI86_RUNTIME_BOOTING = 0,
    PI86_RUNTIME_IDLE,
    PI86_RUNTIME_LOADING,
    PI86_RUNTIME_READY,
    PI86_RUNTIME_RUNNING,
    PI86_RUNTIME_STOPPED,
    PI86_RUNTIME_FAULT,
} pi86_runtime_state_t;

typedef enum {
    PI86_CAP_AVAILABLE = 0,
    PI86_CAP_VALIDATED_NOT_INTEGRATED,
    PI86_CAP_NOT_IMPLEMENTED,
    PI86_CAP_UNAVAILABLE,
    PI86_CAP_FAULT,
} pi86_capability_state_t;

typedef enum {
    PI86_CAP_HOST_CDC = 0,
    PI86_CAP_HOST_HID_RECORDS,
    PI86_CAP_V30_BUS_ENGINE,
    PI86_CAP_V30_INTERRUPTS,
    PI86_CAP_HEARTBEAT,
    PI86_CAP_WORKLOAD_UPLOAD,
    PI86_CAP_WORKLOAD_CONTROL,
    PI86_CAP_V30_STDIO,
    PI86_CAP_SHARED_MEMORY,
    PI86_CAP_FLASH_FAT,
    PI86_CAP_SD_FAT,
    PI86_CAP_BUS_TRACE,
    PI86_CAP_TIMEOUT_RESTART,
    PI86_CAP_HOST_BOOTLOADER,
    PI86_CAP_DVI_OUTPUT,
    PI86_CAP_PIO_USB,
    PI86_CAP_COUNT,
} pi86_capability_id_t;

typedef struct {
    pi86_runtime_state_t state;
    pi86_board_resources_t board_resources;
    pi86_psram_backing_t psram;
    pi86_workload_manager_t workload;
    pi86_capability_state_t capabilities[PI86_CAP_COUNT];
} pi86_runtime_t;

void pi86_runtime_init(pi86_runtime_t *runtime);
const char *pi86_runtime_state_name(pi86_runtime_state_t state);
const char *pi86_capability_name(pi86_capability_id_t capability);
const char *pi86_capability_state_name(pi86_capability_state_t state);
void pi86_runtime_print_identity(void);
void pi86_runtime_print_status(const pi86_runtime_t *runtime);

/* The caller must first acknowledge the Host command.  This function then
 * holds the V30 in RESET, stops CLK low, publishes dirty PSRAM state, and
 * enters the RP2350 ROM USB UF2 boot path. */
void __attribute__((noreturn))
pi86_runtime_enter_bootloader(pi86_runtime_t *runtime);

#endif
