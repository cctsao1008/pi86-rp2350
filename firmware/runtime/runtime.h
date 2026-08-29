#ifndef RP86_RUNTIME_H
#define RP86_RUNTIME_H

#include <stddef.h>

#include "board/rp2350_pizero_resources.h"
#include "memory/backing.h"
#include "memory/psram_backing.h"
#include "runtime/workload_manager.h"

typedef enum {
    RP86_RUNTIME_BOOTING = 0,
    RP86_RUNTIME_IDLE,
    RP86_RUNTIME_LOADING,
    RP86_RUNTIME_STAGED,
    RP86_RUNTIME_RUNNING,
    RP86_RUNTIME_STOPPED,
    RP86_RUNTIME_FAULTED,
} rp86_runtime_state_t;

typedef enum {
    RP86_CAP_AVAILABLE = 0,
    RP86_CAP_VALIDATED_NOT_INTEGRATED,
    RP86_CAP_NOT_IMPLEMENTED,
    RP86_CAP_UNAVAILABLE,
    RP86_CAP_FAULTED,
} rp86_capability_state_t;

typedef enum {
    RP86_CAP_HOST_CDC = 0,
    RP86_CAP_HOST_HID_RECORDS,
    RP86_CAP_PROCESSOR_BUS,
    RP86_CAP_PROCESSOR_INTERRUPTS,
    RP86_CAP_PROCESSOR_LIVENESS,
    RP86_CAP_WORKLOAD_UPLOAD,
    RP86_CAP_WORKLOAD_CONTROL,
    RP86_CAP_PROCESSOR_STDIO,
    RP86_CAP_SHARED_MEMORY,
    RP86_CAP_FLASH_FAT,
    RP86_CAP_SD_FAT,
    RP86_CAP_BUS_TRACE,
    RP86_CAP_TIMEOUT_RESTART,
    RP86_CAP_HOST_BOOTLOADER,
    RP86_CAP_DVI_OUTPUT,
    RP86_CAP_PIO_USB,
    RP86_CAP_COUNT,
} rp86_capability_id_t;

typedef struct {
    rp86_runtime_state_t state;
    rp86_board_resources_t board_resources;
    rp86_psram_backing_t psram;
    rp86_memory_backing_t workload_memory;
    rp86_workload_manager_t workload;
    rp86_capability_state_t capabilities[RP86_CAP_COUNT];
} rp86_runtime_t;

void rp86_runtime_init(rp86_runtime_t *runtime);
const char *rp86_runtime_state_name(rp86_runtime_state_t state);
const char *rp86_capability_name(rp86_capability_id_t capability);
const char *rp86_capability_state_name(rp86_capability_state_t state);
void rp86_runtime_print_identity(void);
void rp86_runtime_print_status(const rp86_runtime_t *runtime);

/* The caller must first acknowledge the Host command.  This function then
 * holds the processor in RESET, stops CLK low, publishes dirty PSRAM state, and
 * enters the RP2350 ROM USB UF2 boot path. */
void __attribute__((noreturn))
rp86_runtime_enter_bootloader(rp86_runtime_t *runtime);

#endif
