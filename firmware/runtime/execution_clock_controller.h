#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

typedef enum {
    RP86_EXECUTION_CLOCK_STOPPED = 0,
    RP86_EXECUTION_CLOCK_FREE_RUNNING,
    RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
} rp86_execution_clock_mode_t;

typedef struct {
    PIO pio;
    uint sm;
    uint free_running_offset;
    uint clock_stepped_offset;
    uint32_t free_running_hz;
    uint32_t clock_stepped_pio_hz;
    rp86_execution_clock_mode_t mode;
} rp86_execution_clock_t;

/*
 * Own one PIO state machine and both execution-clock programs. Initialization
 * parks CLK low in CLOCK_STEPPED mode; no processor pulse is issued until
 * rp86_execution_clock_step() is called.
 */
bool rp86_execution_clock_init(rp86_execution_clock_t *clock, PIO pio,
                               uint32_t free_running_hz,
                               uint32_t clock_stepped_pio_hz);

bool rp86_execution_clock_parameters_valid(uint32_t clk_sys_hz,
                                           uint32_t free_running_hz,
                                           uint32_t clock_stepped_pio_hz);

/* CLOCK_STEPPED -> FREE_RUNNING is legal only while the stepped controller is
 * parked at CLK=LOW. FREE_RUNNING -> CLOCK_STEPPED requests a PIO stop token;
 * the state machine acknowledges only after completing a low half-cycle. */
bool rp86_execution_clock_set_mode(rp86_execution_clock_t *clock,
                                   rp86_execution_clock_mode_t mode,
                                   uint32_t timeout_us);

bool rp86_execution_clock_step(rp86_execution_clock_t *clock);
void rp86_execution_clock_stop_low(rp86_execution_clock_t *clock,
                                   uint32_t timeout_us);

const char *rp86_execution_clock_mode_name(rp86_execution_clock_mode_t mode);
