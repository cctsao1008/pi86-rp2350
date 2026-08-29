#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "runtime/execution_clock_controller.h"

/*
 * Reusable 8086-class processor-bus primitives.
 *
 * Scope:
 * - preserves the established Pi86 HAT clock-step cadence;
 * - decodes signal identity using firmware/processor/processor_bus_pins.h;
 * - exposes raw A0 and active-low BHE/INTA values explicitly;
 * - does not decide the system memory map or I/O policy.
 */

typedef enum {
    RP86_PROCESSOR_BUS_LANES_NONE = 0,
    RP86_PROCESSOR_BUS_LANE_LOW   = 1u << 0,
    RP86_PROCESSOR_BUS_LANE_HIGH  = 1u << 1,
    RP86_PROCESSOR_BUS_LANES_WORD = RP86_PROCESSOR_BUS_LANE_LOW | RP86_PROCESSOR_BUS_LANE_HIGH,
} rp86_processor_bus_lanes_t;

typedef enum {
    RP86_PROCESSOR_BUS_CYCLE_MEM_READ,
    RP86_PROCESSOR_BUS_CYCLE_MEM_WRITE,
    RP86_PROCESSOR_BUS_CYCLE_IO_READ,
    RP86_PROCESSOR_BUS_CYCLE_IO_WRITE,
    RP86_PROCESSOR_BUS_CYCLE_INTERRUPT_ACK,
    RP86_PROCESSOR_BUS_CYCLE_UNSUPPORTED,
} rp86_processor_bus_cycle_type_t;

typedef struct {
    uint32_t t1_sample;
    uint32_t control_sample;
    uint32_t address;
    uint32_t idle_steps;
    uint8_t a0;
    uint8_t bhe_n;
    uint8_t iom;
    uint8_t dtr;
    uint8_t inta_n;
    rp86_processor_bus_lanes_t lanes;
    rp86_processor_bus_cycle_type_t type;
} rp86_processor_bus_cycle_t;

typedef struct {
    rp86_execution_clock_t execution_clock;
} rp86_processor_bus_t;

void rp86_processor_bus_prepare_header_high_z(void);
bool rp86_processor_bus_init(rp86_processor_bus_t *bus, PIO pio,
                             uint32_t free_running_hz,
                             uint32_t clock_stepped_pio_hz);
void rp86_processor_bus_hold_reset(bool asserted);
void rp86_processor_bus_set_intr(bool asserted);
uint32_t rp86_processor_bus_step(rp86_processor_bus_t *bus);
void rp86_processor_bus_reset_sequence(rp86_processor_bus_t *bus, uint reset_clocks);
bool rp86_processor_bus_set_execution_clock_mode(
    rp86_processor_bus_t *bus, rp86_execution_clock_mode_t mode,
    uint32_t timeout_us);
rp86_execution_clock_mode_t rp86_processor_bus_execution_clock_mode(
    const rp86_processor_bus_t *bus);


bool rp86_processor_bus_wait_cycle(rp86_processor_bus_t *bus,
                        uint max_idle_steps,
                        rp86_processor_bus_cycle_t *cycle);

uint16_t rp86_processor_bus_decode_ad(uint32_t sample);

void rp86_processor_bus_release_ad(void);
void rp86_processor_bus_drive_data(uint16_t value, rp86_processor_bus_lanes_t lanes);

/* Advance two pi86 data clocks while the host is driving a read response. */
void rp86_processor_bus_complete_read(rp86_processor_bus_t *bus,
                           uint16_t *readback1,
                           uint16_t *readback2);

/*
 * Capture CPU write data using the same phase proven by Gate 6:
 * control_sample, then two subsequent processor CLK steps.
 */
void rp86_processor_bus_complete_write(rp86_processor_bus_t *bus,
                            const rp86_processor_bus_cycle_t *cycle,
                            uint16_t *sample0,
                            uint16_t *sample1,
                            uint16_t *sample2);

void rp86_processor_bus_safe_halt(rp86_processor_bus_t *bus, uint reset_clocks);

/*
 * Standard test shutdown used by gate firmware: assert RESET, advance the
 * established eight reset clocks, stop the PIO clock, force CLK low, and
 * leave the multiplexed AD bus high-Z.
 */
static inline void rp86_processor_bus_shutdown(rp86_processor_bus_t *bus) {
    rp86_processor_bus_safe_halt(bus, 8u);
}
