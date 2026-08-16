#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

/*
 * Reusable V30 bus-service primitives.
 *
 * Scope:
 * - preserves the early-pi86 software-stepped CLK() cadence;
 * - decodes signal identity using firmware/v30/v30_pins.h;
 * - exposes raw A0 and active-low BHE/INTA values explicitly;
 * - does not decide the system memory map or I/O policy.
 */

typedef enum {
    V30_BUS_LANES_NONE = 0,
    V30_BUS_LANE_LOW   = 1u << 0,
    V30_BUS_LANE_HIGH  = 1u << 1,
    V30_BUS_LANES_WORD = V30_BUS_LANE_LOW | V30_BUS_LANE_HIGH,
} v30_bus_lanes_t;

typedef enum {
    V30_BUS_CYCLE_MEM_READ,
    V30_BUS_CYCLE_MEM_WRITE,
    V30_BUS_CYCLE_IO_READ,
    V30_BUS_CYCLE_IO_WRITE,
    V30_BUS_CYCLE_INTERRUPT_ACK,
    V30_BUS_CYCLE_UNSUPPORTED,
} v30_bus_cycle_type_t;

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
    v30_bus_lanes_t lanes;
    v30_bus_cycle_type_t type;
} v30_bus_cycle_t;

typedef struct {
    PIO pio;
    uint sm;
    uint program_offset;
} v30_bus_t;

void v30_bus_prepare_header_high_z(void);
void v30_bus_init(v30_bus_t *bus, PIO pio, uint32_t step_pio_clock_hz);
void v30_bus_hold_reset(bool asserted);
void v30_bus_set_intr(bool asserted);
uint32_t v30_bus_step(v30_bus_t *bus);
void v30_bus_reset_sequence(v30_bus_t *bus, uint reset_clocks);

bool v30_bus_wait_cycle(v30_bus_t *bus,
                        uint max_idle_steps,
                        v30_bus_cycle_t *cycle);

uint16_t v30_bus_decode_ad(uint32_t sample);

void v30_bus_release_ad(void);
void v30_bus_drive_data(uint16_t value, v30_bus_lanes_t lanes);

/* Advance two pi86 data clocks while the host is driving a read response. */
void v30_bus_complete_read(v30_bus_t *bus,
                           uint16_t *readback1,
                           uint16_t *readback2);

/*
 * Capture CPU write data using the same phase proven by Gate 6:
 * control_sample, then two subsequent pi86 CLK() steps.
 */
void v30_bus_complete_write(v30_bus_t *bus,
                            const v30_bus_cycle_t *cycle,
                            uint16_t *sample0,
                            uint16_t *sample1,
                            uint16_t *sample2);

void v30_bus_safe_halt(v30_bus_t *bus, uint reset_clocks);

/*
 * Standard test shutdown used by gate firmware: assert RESET, advance the
 * established eight reset clocks, stop the PIO clock, force CLK low, and
 * leave the multiplexed AD bus high-Z.
 */
static inline void v30_bus_shutdown(v30_bus_t *bus) {
    v30_bus_safe_halt(bus, 8u);
}
