#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "memory/memory.h"
#include "paced_general_workload.h"
#include "runtime/paced_bus_engine.h"
#include "v30/v30_bus.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define RESET_CLOCKS                   8u
#define MAX_IDLE_STEPS                64u
#define MAX_BUS_CYCLES              4096u

#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x40000u
#define LOAD_ADDRESS            0x10000u
#define V30_ROM_BASE             0xF0000u
#define V30_ROM_SIZE             0x10000u
#define RESET_VECTOR             0xFFFF0u
#define RESULT_PORT                 0xE8u
#define EXIT_PORT                   0xE6u
#define EXPECTED_RESULT           0x0037u
#define EXPECTED_EXIT             0x600Du

typedef struct {
    uint16_t result;
    uint16_t exit_code;
    uint32_t result_writes;
    uint32_t exit_writes;
} test_io_t;

static uint8_t ram[RAM_SIZE];
static uint8_t rom[V30_ROM_SIZE];

static uint16_t lane_value(v30_bus_lanes_t lanes, uint16_t value) {
    if (lanes == V30_BUS_LANE_HIGH) return (uint16_t)(value >> 8u);
    return value;
}

static bool test_io_read(void *context, uint16_t port,
                         v30_bus_lanes_t lanes, uint16_t *value) {
    (void)context;
    (void)port;
    (void)lanes;
    (void)value;
    return false;
}

static bool test_io_write(void *context, uint16_t port,
                          v30_bus_lanes_t lanes, uint16_t value) {
    test_io_t *io = (test_io_t *)context;
    const uint16_t selected = lane_value(lanes, value);
    if (port == RESULT_PORT) {
        io->result = selected;
        ++io->result_writes;
        return true;
    }
    if (port == EXIT_PORT) {
        io->exit_code = selected;
        ++io->exit_writes;
        return true;
    }
    return false;
}

static void prepare_images(void) {
    memset(ram, 0, sizeof ram);
    memset(rom, 0x90, sizeof rom);
    hard_assert(LOAD_ADDRESS + paced_general_workload_size <=
                RAM_BASE + RAM_SIZE);
    memcpy(&ram[LOAD_ADDRESS - RAM_BASE], paced_general_workload_data,
           paced_general_workload_size);

    /* Reset handoff: JMP FAR 1000:0000. */
    const uint32_t reset = RESET_VECTOR - V30_ROM_BASE;
    rom[reset + 0u] = 0xEAu;
    rom[reset + 1u] = 0x00u;
    rom[reset + 2u] = 0x00u;
    rom[reset + 3u] = 0x00u;
    rom[reset + 4u] = 0x10u;
    rom[reset + 5u] = 0x90u;
}

int main(void) {
    v30_bus_prepare_header_high_z();
    prepare_images();

    pi86_memory_t memory;
    pi86_memory_init(&memory, ram, RAM_BASE, RAM_SIZE,
                     rom, V30_ROM_BASE, V30_ROM_SIZE);

    test_io_t test_io = {0};
    const pi86_paced_io_t io = {
        .context = &test_io,
        .read = test_io_read,
        .write = test_io_write,
    };

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nRP86 software-paced general Internal-SRAM runtime\n");
    printf("Physical clock policy = software-issued complete pulses; no READY dependency\n");
    printf("RAM = 00000h-3FFFFh, workload = %05Xh (%lu bytes), stack = 3FFFEh\n",
           LOAD_ADDRESS, (unsigned long)paced_general_workload_size);
    printf("Proof = LOOP + taken branch + PUSH/POP + byte/word RAM + word OUT\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, RESET_CLOCKS);

    pi86_paced_stats_t stats = {0};
    bool service_ok = true;
    while (stats.cycles < MAX_BUS_CYCLES && test_io.exit_writes == 0u) {
        if (!pi86_paced_service_cycle(&bus, &memory, &io,
                                      MAX_IDLE_STEPS, &stats)) {
            service_ok = false;
            break;
        }
    }

    v30_bus_safe_halt(&bus, RESET_CLOCKS);

    const bool reset_ok = stats.first_cycle_seen &&
                          stats.first_address == RESET_VECTOR;
    const bool result_ok = test_io.result_writes == 1u &&
                           test_io.result == EXPECTED_RESULT;
    const bool exit_ok = test_io.exit_writes == 1u &&
                         test_io.exit_code == EXPECTED_EXIT;
    const bool memory_ok = stats.memory_writes >= 30u;
    const bool pass = service_ok && reset_ok && result_ok && exit_ok &&
                      memory_ok && !stats.unmapped && !stats.invalid_lane &&
                      !stats.pad_mismatch;

    printf("[PACED NATIVE EXECUTION]\n");
    printf("First reset fetch        = %05lX %s\n",
           (unsigned long)stats.first_address, reset_ok ? "PASS" : "FAIL");
    printf("Serviced bus cycles      = %lu\n", (unsigned long)stats.cycles);
    printf("Memory reads / writes    = %lu / %lu\n",
           (unsigned long)stats.memory_reads,
           (unsigned long)stats.memory_writes);
    printf("I/O reads / writes       = %lu / %lu\n",
           (unsigned long)stats.io_reads,
           (unsigned long)stats.io_writes);
    printf("Native result            = %04X (expected %04X) %s\n",
           test_io.result, EXPECTED_RESULT, result_ok ? "PASS" : "FAIL");
    printf("Native exit              = %04X (expected %04X) %s\n",
           test_io.exit_code, EXPECTED_EXIT, exit_ok ? "PASS" : "FAIL");
    printf("General control flow     = LOOP / branch / stack / RAM %s\n",
           memory_ok ? "PASS" : "FAIL");
    printf("Unmapped/lane/pad faults = %u/%u/%u\n",
           stats.unmapped, stats.invalid_lane, stats.pad_mismatch);
    printf("Last cycle               = %s @ %05lX\n",
           pi86_paced_cycle_name(stats.last_type),
           (unsigned long)stats.last_address);
    printf("PACED NATIVE RUNTIME RESULT = %s\n", pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
