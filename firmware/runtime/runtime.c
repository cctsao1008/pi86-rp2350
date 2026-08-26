#include "runtime/runtime.h"

#include <stdio.h>
#include <string.h>

#include "board/rp2350_pizero.h"
#include "hardware/gpio.h"
#include "memory/internal_sram_backing.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "v30/v30_pins.h"

static const char *const capability_names[PI86_CAP_COUNT] = {
    [PI86_CAP_HOST_CDC] = "Host CDC diagnostics",
    [PI86_CAP_HOST_HID_RECORDS] = "Host 64-byte HID records",
    [PI86_CAP_V30_BUS_ENGINE] = "physical 8086-class PIO/DMA bus",
    [PI86_CAP_V30_INTERRUPTS] = "physical INTR / two-cycle INTA",
    [PI86_CAP_HEARTBEAT] = "persistent processor heartbeat",
    [PI86_CAP_WORKLOAD_UPLOAD] = "native workload upload",
    [PI86_CAP_WORKLOAD_CONTROL] = "workload run / stop / restart",
    [PI86_CAP_V30_STDIO] = "V30 stdin / stdout",
    [PI86_CAP_SHARED_MEMORY] = "Host / V30 shared memory",
    [PI86_CAP_FLASH_FAT] = "flash: FAT volume",
    [PI86_CAP_SD_FAT] = "sd: FAT volume",
    [PI86_CAP_BUS_TRACE] = "retained physical bus trace",
    [PI86_CAP_TIMEOUT_RESTART] = "timeout / fault / restart",
    [PI86_CAP_HOST_BOOTLOADER] = "Host-directed UF2 boot",
    [PI86_CAP_DVI_OUTPUT] = "Mini HDMI / DVI output",
    [PI86_CAP_PIO_USB] = "PIO-USB host / device",
};

void pi86_runtime_init(pi86_runtime_t *runtime) {
    memset(runtime, 0, sizeof *runtime);
    runtime->state = PI86_RUNTIME_BOOTING;

    runtime->capabilities[PI86_CAP_HOST_CDC] = PI86_CAP_AVAILABLE;

    /* These mechanisms have accepted physical evidence in retained targets.
     * They are deliberately not reported AVAILABLE until canonical main.c
     * owns the same implementation. */
    runtime->capabilities[PI86_CAP_HOST_HID_RECORDS] =
        PI86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[PI86_CAP_V30_BUS_ENGINE] =
        PI86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[PI86_CAP_V30_INTERRUPTS] =
        PI86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[PI86_CAP_HEARTBEAT] =
        PI86_CAP_VALIDATED_NOT_INTEGRATED;
    runtime->capabilities[PI86_CAP_BUS_TRACE] =
        PI86_CAP_VALIDATED_NOT_INTEGRATED;

    runtime->capabilities[PI86_CAP_WORKLOAD_UPLOAD] =
        PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_WORKLOAD_CONTROL] =
        PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_V30_STDIO] = PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_SHARED_MEMORY] = PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_FLASH_FAT] = PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_SD_FAT] = PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_TIMEOUT_RESTART] =
        PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_HOST_BOOTLOADER] =
        PI86_CAP_AVAILABLE;
    runtime->capabilities[PI86_CAP_DVI_OUTPUT] = PI86_CAP_NOT_IMPLEMENTED;
    runtime->capabilities[PI86_CAP_PIO_USB] = PI86_CAP_NOT_IMPLEMENTED;

    /* Establish all RP2350-PiZero onboard interface pins before any service
     * can claim them. This does not enable SD, DVI or PIO-USB; it guarantees
     * that unimplemented services remain electrically passive. */
    pi86_board_resources_safe_init(&runtime->board_resources);

    /* Canonical firmware owns a conservative power-on state even before the
     * validated bus engine is integrated. */
    gpio_init(V30_PIN_RESET);
    gpio_put(V30_PIN_RESET, true);
    gpio_set_dir(V30_PIN_RESET, GPIO_OUT);
    gpio_init(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);

    (void)pi86_psram_backing_init(&runtime->psram);
    pi86_internal_sram_backing_init(&runtime->workload_memory);
    pi86_workload_manager_init(&runtime->workload,
                               &runtime->workload_memory);
    runtime->state = PI86_RUNTIME_IDLE;
}

const char *pi86_runtime_state_name(pi86_runtime_state_t state) {
    switch (state) {
        case PI86_RUNTIME_BOOTING: return "BOOTING";
        case PI86_RUNTIME_IDLE: return "IDLE";
        case PI86_RUNTIME_LOADING: return "LOADING";
        case PI86_RUNTIME_READY: return "READY";
        case PI86_RUNTIME_RUNNING: return "RUNNING";
        case PI86_RUNTIME_STOPPED: return "STOPPED";
        case PI86_RUNTIME_FAULT: return "FAULT";
        default: return "UNKNOWN";
    }
}

const char *pi86_capability_name(pi86_capability_id_t capability) {
    if ((unsigned)capability >= PI86_CAP_COUNT) return "unknown capability";
    return capability_names[capability];
}

const char *pi86_capability_state_name(pi86_capability_state_t state) {
    switch (state) {
        case PI86_CAP_AVAILABLE: return "AVAILABLE";
        case PI86_CAP_VALIDATED_NOT_INTEGRATED:
            return "VALIDATED / NOT INTEGRATED";
        case PI86_CAP_NOT_IMPLEMENTED: return "NOT IMPLEMENTED";
        case PI86_CAP_UNAVAILABLE: return "UNAVAILABLE";
        case PI86_CAP_FAULT: return "FAULT";
        default: return "UNKNOWN";
    }
}

void pi86_runtime_print_identity(void) {
    printf("\npi86-rp2350\n");
    printf("Runtime    : Host-Managed Bare-Metal Processor Runtime\n");
    printf("Host       : Runtime Controller\n");
    printf("RP2350     : Companion Resource and Bus Controller\n");
    printf("Processor  : Host-declared Intel 8086 or NEC V30\n");
    printf("Execution  : native 8086-class bare-metal workloads\n");
    printf("Board      : Waveshare RP2350-PiZero\n");
    printf("Interface  : original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("Header GPIO: GPIO%u..GPIO%u\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("V30 signals: CLK GPIO%u / RESET GPIO%u\n",
           V30_PIN_CLK, V30_PIN_RESET);
}

void pi86_runtime_print_status(const pi86_runtime_t *runtime) {
    printf("\n[RUNTIME STATUS]\n");
    printf("State                      = %s\n",
           pi86_runtime_state_name(runtime->state));
    printf("External PSRAM configured  = %s\n",
           PI86_HAS_EXTERNAL_PSRAM ? "YES" : "NO");
#if PI86_HAS_EXTERNAL_PSRAM
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
           pi86_workload_state_name(runtime->workload.state));
    if (runtime->workload.state != PI86_WORKLOAD_STATE_EMPTY)
        printf(" (%lu bytes)",
               (unsigned long)runtime->workload.manifest.image_size);
    printf("\n");
    printf("Onboard GPIO safe state    = %s\n",
           runtime->board_resources.safe_state_initialized ? "PASS" : "FAIL");
    printf("MicroSD hardware           = %s\n",
           PI86_HAS_SDCARD ? "PRESENT" : "ABSENT");
    printf("MicroSD GPIO30/31/40-43    = PASSIVE / NOT CLAIMED\n");
    printf("Mini HDMI/DVI hardware     = %s\n",
           PI86_HAS_DVI ? "PRESENT" : "ABSENT");
    printf("Mini HDMI GPIO32-39/44-46  = PASSIVE / NOT CLAIMED\n");
    printf("PIO-USB hardware           = %s\n",
           PI86_HAS_PIO_USB ? "PRESENT" : "ABSENT");
    printf("PIO-USB GPIO28/29          = PASSIVE / NOT CLAIMED\n");
    printf("DVI / PIO-USB concurrency  = MUTUALLY EXCLUSIVE\n");

    printf("\n[CAPABILITY FRAMEWORK]\n");
    for (unsigned i = 0u; i < PI86_CAP_COUNT; ++i) {
        printf("%-27s = %s\n", pi86_capability_name((pi86_capability_id_t)i),
               pi86_capability_state_name(runtime->capabilities[i]));
    }
    printf("\nOne canonical runtime; unavailable features remain explicit.\n");
}

void __attribute__((noreturn))
pi86_runtime_enter_bootloader(pi86_runtime_t *runtime) {
    gpio_put(V30_PIN_RESET, true);
    gpio_put(V30_PIN_CLK, false);
    runtime->state = PI86_RUNTIME_STOPPED;
    if (runtime->psram.available) pi86_psram_publish();
    stdio_flush();
    /*
     * Let Windows receive the CDC acknowledgement before the ROM bootloader
     * tears down this USB device. A shorter delay can complete the hardware
     * transition correctly while making the Host report a false failure.
     */
    sleep_ms(250u);
    reset_usb_boot(0u, 0u);
}
