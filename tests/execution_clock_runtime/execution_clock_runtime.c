#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "execution_clock_transition_workload.h"
#include "memory/memory.h"
#include "processor/processor_bus.h"
#include "processor/processor_bus_pins.h"
#include "runtime/clock_stepped_bus_controller.h"

#define CLOCK_STEPPED_PIO_HZ     2000000u
#define FREE_RUNNING_HZ          1000000u
#define CLOCK_SWITCH_TIMEOUT_US   100000u
#define RESET_CLOCKS                   8u
#define MAX_IDLE_STEPS                64u
#define MAX_BUS_CYCLES              4096u

#define RAM_BASE                  0x00000u
#define RAM_SIZE                  0x40000u
#define LOAD_ADDRESS             0x10000u
#define PROCESSOR_IMAGE_BASE      0xF0000u
#define PROCESSOR_IMAGE_SIZE      0x10000u
#define RESET_VECTOR              0xFFFF0u
#define RESULT_PORT                 0x00E8u
#define EXECUTION_CLOCK_PORT        0x00EAu
#define EXECUTION_CLOCK_FREE        0x0001u
#define EXPECTED_RESULT             0x002Au

typedef struct {
    uint16_t result;
    uint16_t clock_request;
    uint32_t result_writes;
    uint32_t clock_requests;
} transition_io_t;

static uint8_t ram[RAM_SIZE];
static uint8_t rom[PROCESSOR_IMAGE_SIZE];

static uint16_t selected_lane_value(rp86_processor_bus_lanes_t lanes,
                                    uint16_t value) {
    return lanes == RP86_PROCESSOR_BUS_LANE_HIGH ?
        (uint16_t)(value >> 8u) : value;
}

static bool transition_io_read(void *context, uint16_t port,
                               rp86_processor_bus_lanes_t lanes,
                               uint16_t *value) {
    (void)context;
    (void)port;
    (void)lanes;
    (void)value;
    return false;
}

static bool transition_io_write(void *context, uint16_t port,
                                rp86_processor_bus_lanes_t lanes,
                                uint16_t value) {
    transition_io_t *io = (transition_io_t *)context;
    const uint16_t selected = selected_lane_value(lanes, value);
    if (port == RESULT_PORT) {
        io->result = selected;
        ++io->result_writes;
        return true;
    }
    if (port == EXECUTION_CLOCK_PORT) {
        io->clock_request = selected;
        ++io->clock_requests;
        return selected == EXECUTION_CLOCK_FREE;
    }
    return false;
}

static void prepare_images(void) {
    memset(ram, 0, sizeof ram);
    memset(rom, 0x90, sizeof rom);
    hard_assert(LOAD_ADDRESS + execution_clock_transition_workload_size <=
                RAM_BASE + RAM_SIZE);
    memcpy(&ram[LOAD_ADDRESS - RAM_BASE],
           execution_clock_transition_workload_data,
           execution_clock_transition_workload_size);

    /* Reset handoff: JMP FAR 1000:0000. */
    const uint32_t reset = RESET_VECTOR - PROCESSOR_IMAGE_BASE;
    rom[reset + 0u] = 0xEAu;
    rom[reset + 1u] = 0x00u;
    rom[reset + 2u] = 0x00u;
    rom[reset + 3u] = 0x00u;
    rom[reset + 4u] = 0x10u;
    rom[reset + 5u] = 0x90u;
}

static uint32_t observe_free_running_edges(uint32_t duration_us) {
    uint32_t edges = 0u;
    bool previous = gpio_get(RP86_PROCESSOR_PIN_CLK);
    const uint64_t deadline = time_us_64() + duration_us;
    while (time_us_64() <= deadline) {
        const bool current = gpio_get(RP86_PROCESSOR_PIN_CLK);
        if (current != previous) {
            ++edges;
            previous = current;
        }
    }
    return edges;
}

int main(void) {
    rp86_processor_bus_prepare_header_high_z();
    prepare_images();

    rp86_memory_t memory;
    rp86_memory_init(&memory, ram, RAM_BASE, RAM_SIZE,
                     rom, PROCESSOR_IMAGE_BASE, PROCESSOR_IMAGE_SIZE);

    transition_io_t transition = {0};
    const rp86_clock_stepped_io_t io = {
        .context = &transition,
        .read = transition_io_read,
        .write = transition_io_write,
    };

    rp86_processor_bus_hold_reset(true);
    rp86_processor_bus_set_intr(false);
    rp86_processor_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nRP86 execution-clock transition runtime\n");
    printf("Start = CLOCK_STEPPED; native INT 60h requests FREE_RUNNING\n");
    printf("Return = RP2350 safe-stop acknowledgement at CLK=LOW\n\n");
    fflush(stdout);

    rp86_processor_bus_t bus;
    hard_assert(rp86_processor_bus_init(
        &bus, pio0, FREE_RUNNING_HZ, CLOCK_STEPPED_PIO_HZ));
    rp86_processor_bus_reset_sequence(&bus, RESET_CLOCKS);

    rp86_clock_stepped_stats_t stats = {0};
    bool service_ok = true;
    bool request_boundary = false;
    while (stats.cycles < MAX_BUS_CYCLES) {
        if (!rp86_clock_stepped_service_cycle(
                &bus, &memory, &io, MAX_IDLE_STEPS, &stats)) {
            service_ok = false;
            break;
        }
        if (transition.clock_requests == 1u) {
            request_boundary = !stats.unmapped && !stats.invalid_lane &&
                !stats.pad_mismatch &&
                !gpio_get(RP86_PROCESSOR_PIN_CLK);
            break;
        }
    }

    const bool request_ok = transition.clock_requests == 1u &&
        transition.clock_request == EXECUTION_CLOCK_FREE;
    const bool result_ok = transition.result_writes == 1u &&
        transition.result == EXPECTED_RESULT;
    const bool entered_free = request_boundary &&
        rp86_processor_bus_set_execution_clock_mode(
            &bus, RP86_EXECUTION_CLOCK_FREE_RUNNING,
            CLOCK_SWITCH_TIMEOUT_US);
    const uint32_t free_running_edges = entered_free ?
        observe_free_running_edges(2000u) : 0u;
    const bool free_running_ok = entered_free && free_running_edges >= 100u;
    const bool returned_stepped = free_running_ok &&
        rp86_processor_bus_set_execution_clock_mode(
            &bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
            CLOCK_SWITCH_TIMEOUT_US);
    const bool safe_low = returned_stepped &&
        !gpio_get(RP86_PROCESSOR_PIN_CLK);

    rp86_processor_bus_safe_halt(&bus, RESET_CLOCKS);

    const bool reset_ok = stats.first_cycle_seen &&
        stats.first_address == RESET_VECTOR;
    const bool pass = service_ok && reset_ok && result_ok && request_ok &&
        free_running_ok && returned_stepped && safe_low;

    printf("[EXECUTION CLOCK MODE TRANSITION]\n");
    printf("First reset fetch       = %05lX %s\n",
           (unsigned long)stats.first_address, reset_ok ? "PASS" : "FAIL");
    printf("Clock-stepped cycles    = %lu\n", (unsigned long)stats.cycles);
    printf("Native result           = %04X (expected %04X) %s\n",
           transition.result, EXPECTED_RESULT, result_ok ? "PASS" : "FAIL");
    printf("INT 60h clock request   = %04X %s\n",
           transition.clock_request, request_ok ? "PASS" : "FAIL");
    printf("Request cycle boundary = %s\n",
           request_boundary ? "CLK=LOW PASS" : "FAIL");
    printf("FREE_RUNNING edges      = %lu %s\n",
           (unsigned long)free_running_edges,
           free_running_ok ? "PASS" : "FAIL");
    printf("Safe return to stepped  = %s\n",
           safe_low ? "CLK=LOW PASS" : "FAIL");
    printf("Last observed cycle     = %s @ %05lX\n",
           rp86_clock_stepped_cycle_name(stats.last_type),
           (unsigned long)stats.last_address);
    printf("Unmapped/lane/pad faults= %u/%u/%u\n",
           stats.unmapped, stats.invalid_lane, stats.pad_mismatch);
    printf("EXECUTION CLOCK TRANSITION RESULT = %s\n",
           pass ? "PASS" : "FAIL");
    printf("Processor halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
