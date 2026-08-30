#include "runtime/runtime.h"

#include <stdio.h>
#include <string.h>

#include "board/rp2350_pizero.h"
#include "hardware/gpio.h"
#include "memory/internal_sram_backing.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "bus/processor_bus_pins.h"

static const char *const capability_names[RP86_CAP_COUNT] = {
    [RP86_CAP_HOST_CDC] = "Host CDC diagnostics",
    [RP86_CAP_HOST_HID_RECORDS] = "Host 64-byte HID records",
    [RP86_CAP_PROCESSOR_BUS] = "physical 8086-class PIO/DMA bus",
    [RP86_CAP_PROCESSOR_INTERRUPTS] = "physical INTR / two-cycle INTA",
    [RP86_CAP_PROCESSOR_LIVENESS] = "processor liveness monitoring",
    [RP86_CAP_WORKLOAD_UPLOAD] = "native workload upload",
    [RP86_CAP_WORKLOAD_CONTROL] = "workload run / stop / restart",
    [RP86_CAP_PROCESSOR_STDIO] = "processor stdin / stdout",
    [RP86_CAP_SHARED_MEMORY] = "Host / processor shared memory",
    [RP86_CAP_FLASH_FAT] = "flash: FAT volume",
    [RP86_CAP_SD_FAT] = "sd: FAT volume",
    [RP86_CAP_BUS_TRACE] = "retained physical bus trace",
    [RP86_CAP_TIMEOUT_RESTART] = "timeout / fault / restart",
    [RP86_CAP_HOST_BOOTLOADER] = "Host-directed UF2 boot",
    [RP86_CAP_DVI_OUTPUT] = "Mini HDMI / DVI output",
    [RP86_CAP_PIO_USB] = "PIO-USB host / device",
};

void rp86_runtime_init(rp86_runtime_t *runtime) {
    memset(runtime, 0, sizeof *runtime);
    runtime->state = RP86_RUNTIME_BOOTING;

    runtime->capabilities[RP86_CAP_HOST_CDC] = RP86_CAP_AVAILABLE;

    /* These mechanisms have accepted physical evidence in retained targets.
     * They are deliberately not reported AVAILABLE until canonical main.c
     * owns the same implementation. */
    runtime->capabilities[RP86_CAP_HOST_HID_RECORDS] =
        RP86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[RP86_CAP_PROCESSOR_BUS] =
        RP86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[RP86_CAP_PROCESSOR_INTERRUPTS] =
        RP86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[RP86_CAP_PROCESSOR_LIVENESS] =
        RP86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[RP86_CAP_BUS_TRACE] =
        RP86_CAP_VALIDATED_NOT_INTEGRATED;

    runtime->capabilities[RP86_CAP_WORKLOAD_UPLOAD] =
        RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_WORKLOAD_CONTROL] =
        RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_PROCESSOR_STDIO] = RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_SHARED_MEMORY] = RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_FLASH_FAT] = RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_SD_FAT] = RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_TIMEOUT_RESTART] =
        RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_HOST_BOOTLOADER] =
        RP86_CAP_AVAILABLE;
    runtime->capabilities[RP86_CAP_DVI_OUTPUT] = RP86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[RP86_CAP_PIO_USB] = RP86_CAP_NOT_IMPLEMENTED;

    /* Establish all RP2350-PiZero onboard interface pins before any service
     * can claim them. This does not enable SD, DVI or PIO-USB; it guarantees
     * that unimplemented services remain electrically passive. */
    rp86_board_resources_safe_init(&runtime->board_resources);

    /* Canonical firmware owns a conservative power-on state even before the
     * validated bus controller is integrated. */
    gpio_init(RP86_PROCESSOR_PIN_RESET);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);
    gpio_init(RP86_PROCESSOR_PIN_CLK);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    (void)rp86_psram_backing_init(&runtime->psram);
    rp86_internal_sram_backing_init(&runtime->workload_memory);
    rp86_workload_manager_init(&runtime->workload,
                               &runtime->workload_memory);
    runtime->state = RP86_RUNTIME_IDLE;
}

const char *rp86_runtime_state_name(rp86_runtime_state_t state) {
    switch (state) {
        case RP86_RUNTIME_BOOTING: return "BOOTING";
        case RP86_RUNTIME_IDLE: return "IDLE";
        case RP86_RUNTIME_LOADING: return "LOADING";
        case RP86_RUNTIME_STAGED: return "STAGED";
        case RP86_RUNTIME_RUNNING: return "RUNNING";
        case RP86_RUNTIME_STOPPED: return "STOPPED";
        case RP86_RUNTIME_FAULTED: return "FAULTED";
        default: return "UNKNOWN";
    }
}

const char *rp86_capability_name(rp86_capability_id_t capability) {
    if ((unsigned)capability >= RP86_CAP_COUNT) return "unknown capability";
    return capability_names[capability];
}

const char *rp86_capability_state_name(rp86_capability_state_t state) {
    switch (state) {
        case RP86_CAP_AVAILABLE: return "AVAILABLE";
        case RP86_CAP_VALIDATED_NOT_INTEGRATED:
            return "VALIDATED / NOT INTEGRATED";
        case RP86_CAP_NOT_IMPLEMENTED: return "NOT IMPLEMENTED";
        case RP86_CAP_UNAVAILABLE: return "UNAVAILABLE";
        case RP86_CAP_FAULTED: return "FAULTED";
        default: return "UNKNOWN";
    }
}

void rp86_runtime_print_identity(void) {
    printf("\npi86-rp2350\n");
    printf("Runtime    : Host-Managed Bare-Metal Processor Runtime\n");
    printf("Host       : Runtime Controller\n");
    printf("RP2350     : Companion Resource and Bus Controller\n");
    printf("Processor  : automatically identified Intel 8086 or NEC V30\n");
    printf("Execution  : native 8086-class bare-metal workloads\n");
    printf("Board      : Waveshare RP2350-PiZero\n");
    printf("Interface  : original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("Header GPIO: GPIO%u..GPIO%u\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("Processor signals: CLK GPIO%u / RESET GPIO%u\n",
           RP86_PROCESSOR_PIN_CLK, RP86_PROCESSOR_PIN_RESET);
}

void rp86_runtime_print_status(const rp86_runtime_t *runtime) {
    printf("\n[RUNTIME STATUS]\n");
    printf("State                      = %s\n",
           rp86_runtime_state_name(runtime->state));
    printf("External PSRAM configured  = %s\n",
           RP86_HAS_EXTERNAL_PSRAM ? "YES" : "NO");
#if RP86_HAS_EXTERNAL_PSRAM
    printf("External PSRAM detected    = %s",
           runtime->psram.available ? "AVAILABLE" : "NOT FOUND");
    if (runtime->psram.available)
        printf(" (%zu bytes / %zu MiB)", runtime->psram.size,
               runtime->psram.size / (1024u * 1024u));
    printf("\n");
#else
    printf("External PSRAM probe       = SKIPPED\n");
#endif
    printf("Workload memory            = %s\n",
           runtime->workload_memory.name);
    printf("Processor memory range     = %05lX-%05lX (%zu KiB)\n",
           (unsigned long)runtime->workload_memory.processor_base,
           (unsigned long)(runtime->workload_memory.processor_base +
                           runtime->workload_memory.size - 1u),
           runtime->workload_memory.size / 1024u);
    printf("External PSRAM role        = OPTIONAL CAPACITY TIER\n");
    printf("Staged workload            = %s",
           rp86_workload_state_name(runtime->workload.state));
    if (runtime->workload.state != RP86_WORKLOAD_STATE_EMPTY)
        printf(" (%lu bytes)",
               (unsigned long)runtime->workload.manifest.image_size);
    printf("\n");
    printf("Onboard GPIO safe state    = %s\n",
           runtime->board_resources.safe_state_initialized ? "PASS" : "FAIL");
    printf("MicroSD hardware           = %s\n",
           RP86_HAS_SDCARD ? "PRESENT" : "ABSENT");
    printf("MicroSD GPIO30/31/40-43    = PASSIVE / NOT CLAIMED\n");
    printf("Mini HDMI/DVI hardware     = %s\n",
           RP86_HAS_DVI ? "PRESENT" : "ABSENT");
    printf("Mini HDMI GPIO32-39/44-46  = PASSIVE / NOT CLAIMED\n");
    printf("PIO-USB hardware           = %s\n",
           RP86_HAS_PIO_USB ? "PRESENT" : "ABSENT");
    printf("PIO-USB GPIO28/29          = PASSIVE / NOT CLAIMED\n");
    printf("DVI / PIO-USB concurrency  = MUTUALLY EXCLUSIVE\n");

    printf("\n[CAPABILITY FRAMEWORK]\n");
    for (unsigned i = 0u; i < RP86_CAP_COUNT; ++i) {
        printf("%-27s = %s\n", rp86_capability_name((rp86_capability_id_t)i),
               rp86_capability_state_name(runtime->capabilities[i]));
    }
    printf("\nOne canonical runtime; unavailable features remain explicit.\n");
}

void __attribute__((noreturn))
rp86_runtime_enter_bootloader(rp86_runtime_t *runtime) {
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    runtime->state = RP86_RUNTIME_STOPPED;
    if (runtime->psram.available) rp86_psram_publish();
    stdio_flush();
    /*
     * Let Windows receive the CDC acknowledgement before the ROM bootloader
     * tears down this USB device. A shorter delay can complete the hardware
     * transition correctly while making the Host report a false failure.
     */
    sleep_ms(250u);
    reset_usb_boot(0u, 0u);
}
