#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "runtime/clock_stepped_bus_controller.h"

static bool completion_ok = true;
static unsigned completions;
static bool completed_drive;
static uint8_t completed_vector;
static unsigned callback_phase;

bool rp86_processor_bus_wait_cycle(rp86_processor_bus_t *bus,
                                   uint max_idle_steps,
                                   rp86_processor_bus_cycle_t *cycle) {
    (void)bus;
    assert(max_idle_steps != 0u);
    memset(cycle, 0, sizeof *cycle);
    cycle->type = RP86_PROCESSOR_BUS_CYCLE_INTERRUPT_ACK;
    cycle->lanes = RP86_PROCESSOR_BUS_LANES_NONE;
    return true;
}

bool rp86_processor_bus_faulted(const rp86_processor_bus_t *bus) {
    (void)bus;
    return false;
}

bool rp86_processor_bus_complete_interrupt_ack(
    rp86_processor_bus_t *bus, bool drive_vector, uint8_t vector) {
    (void)bus;
    ++completions;
    completed_drive = drive_vector;
    completed_vector = vector;
    return completion_ok;
}

void rp86_processor_bus_drive_data(uint16_t value,
                                   rp86_processor_bus_lanes_t lanes) {
    (void)value;
    (void)lanes;
}

bool rp86_processor_bus_complete_read(rp86_processor_bus_t *bus,
                                      uint16_t *readback1,
                                      uint16_t *readback2) {
    (void)bus;
    (void)readback1;
    (void)readback2;
    return true;
}

bool rp86_processor_bus_complete_write(
    rp86_processor_bus_t *bus,
    const rp86_processor_bus_cycle_t *cycle,
    uint16_t *sample0, uint16_t *sample1, uint16_t *sample2) {
    (void)bus;
    (void)cycle;
    if (sample0 != NULL) *sample0 = 0u;
    if (sample1 != NULL) *sample1 = 0u;
    if (sample2 != NULL) *sample2 = 0u;
    return true;
}

static bool ack_policy(void *context, bool *drive_vector, uint8_t *vector) {
    (void)context;
    if (callback_phase++ == 0u) {
        *drive_vector = false;
        *vector = 0u;
    } else {
        *drive_vector = true;
        *vector = 0x21u;
    }
    return true;
}

int main(void) {
    rp86_processor_bus_t bus = {0};
    rp86_memory_t memory = {0};
    rp86_clock_stepped_stats_t stats = {0};
    const rp86_clock_stepped_io_t io = {
        .interrupt_ack = ack_policy,
    };

    assert(rp86_clock_stepped_service_cycle(&bus, &memory, &io, 8u, &stats));
    assert(stats.cycles == 1u && stats.interrupt_acks == 1u);
    assert(stats.interrupt_ack && !stats.invalid_lane && !stats.clock_failure);
    assert(completions == 1u && !completed_drive && completed_vector == 0u);

    assert(rp86_clock_stepped_service_cycle(&bus, &memory, &io, 8u, &stats));
    assert(stats.cycles == 2u && stats.interrupt_acks == 2u);
    assert(completions == 2u && completed_drive && completed_vector == 0x21u);
    assert(stats.last_data_valid && stats.last_data == 0x21u);

    const rp86_clock_stepped_io_t no_ack = {0};
    assert(!rp86_clock_stepped_service_cycle(
        &bus, &memory, &no_ack, 8u, &stats));
    assert(stats.interrupt_ack && completions == 2u);

    completion_ok = false;
    assert(!rp86_clock_stepped_service_cycle(&bus, &memory, &io, 8u, &stats));
    assert(stats.clock_failure && completions == 3u);
    return 0;
}
