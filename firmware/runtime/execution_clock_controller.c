#include "runtime/execution_clock_controller.h"

#include <stddef.h>

#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "clock_stepped_clock.pio.h"
#include "execution_clock_control.pio.h"
#include "bus/processor_bus_pins.h"

#define FREE_RUNNING_PIO_CYCLES_PER_PROCESSOR_CYCLE 10u
#define SAFE_STOP_TOKEN 1u
#define CLOCK_STEP_TIMEOUT_US 100000u

static bool pio_rate_has_valid_divider(uint32_t clk_sys_hz,
                                       uint64_t requested_pio_hz) {
    return requested_pio_hz != 0u && requested_pio_hz <= clk_sys_hz &&
           requested_pio_hz * 65536u > clk_sys_hz;
}

bool rp86_execution_clock_parameters_valid(uint32_t clk_sys_hz,
                                           uint32_t free_running_hz,
                                           uint32_t clock_stepped_pio_hz) {
    const uint64_t free_running_pio_hz =
        (uint64_t)FREE_RUNNING_PIO_CYCLES_PER_PROCESSOR_CYCLE *
        free_running_hz;
    return clk_sys_hz != 0u &&
           pio_rate_has_valid_divider(clk_sys_hz, free_running_pio_hz) &&
           pio_rate_has_valid_divider(clk_sys_hz, clock_stepped_pio_hz);
}

static void force_clock_low(void) {
    gpio_init(RP86_PROCESSOR_PIN_CLK);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_CLK);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);
}

static void prepare_common(rp86_execution_clock_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    pio_sm_clear_fifos(clock->pio, clock->sm);
    pio_sm_restart(clock->pio, clock->sm);
    force_clock_low();
    pio_gpio_init(clock->pio, RP86_PROCESSOR_PIN_CLK);
}

static bool prepare_clock_stepped(rp86_execution_clock_t *clock) {
    prepare_common(clock);
    pio_sm_config config = rp86_clock_stepped_clock_program_get_default_config(
        clock->clock_stepped_offset);
    sm_config_set_set_pins(&config, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(
        &config,
        (float)clock_get_hz(clk_sys) / (float)clock->clock_stepped_pio_hz);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm,
                                   RP86_PROCESSOR_PIN_CLK, 1u, true);
    if (pio_sm_init(clock->pio, clock->sm,
                    clock->clock_stepped_offset, &config) != PICO_OK)
        return false;
    pio_sm_set_pins_with_mask(clock->pio, clock->sm, 0u,
                              1u << RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
    clock->mode = RP86_EXECUTION_CLOCK_CLOCK_STEPPED;
    return true;
}

static bool prepare_free_running(rp86_execution_clock_t *clock) {
    if (gpio_get(RP86_PROCESSOR_PIN_CLK)) return false;
    prepare_common(clock);
    pio_sm_config config =
        rp86_free_running_clock_control_program_get_default_config(
            clock->free_running_offset);
    sm_config_set_set_pins(&config, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(
        &config,
        (float)clock_get_hz(clk_sys) /
            ((float)FREE_RUNNING_PIO_CYCLES_PER_PROCESSOR_CYCLE *
             (float)clock->free_running_hz));
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm,
                                   RP86_PROCESSOR_PIN_CLK, 1u, true);
    if (pio_sm_init(clock->pio, clock->sm,
                    clock->free_running_offset, &config) != PICO_OK)
        return false;
    pio_sm_exec(clock->pio, clock->sm, pio_encode_set(pio_x, 0u));
    pio_sm_set_pins_with_mask(clock->pio, clock->sm, 0u,
                              1u << RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
    clock->mode = RP86_EXECUTION_CLOCK_FREE_RUNNING;
    return true;
}

static bool stop_free_running_low(rp86_execution_clock_t *clock,
                                  uint32_t timeout_us) {
    const uint64_t deadline = time_us_64() + timeout_us;
    while (pio_sm_is_tx_fifo_full(clock->pio, clock->sm)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    pio_sm_put(clock->pio, clock->sm, SAFE_STOP_TOKEN);
    while (pio_sm_is_rx_fifo_empty(clock->pio, clock->sm)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    (void)pio_sm_get(clock->pio, clock->sm);
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    force_clock_low();
    clock->mode = RP86_EXECUTION_CLOCK_STOPPED;
    return true;
}

bool rp86_execution_clock_init(rp86_execution_clock_t *clock, PIO pio,
                               uint32_t free_running_hz,
                               uint32_t clock_stepped_pio_hz) {
    if (clock == NULL ||
        !rp86_execution_clock_parameters_valid(clock_get_hz(clk_sys),
                                               free_running_hz,
                                               clock_stepped_pio_hz))
        return false;
    clock->pio = pio;
    clock->sm = pio_claim_unused_sm(pio, true);
    clock->free_running_offset = pio_add_program(
        pio, &rp86_free_running_clock_control_program);
    clock->clock_stepped_offset = pio_add_program(
        pio, &rp86_clock_stepped_clock_program);
    clock->free_running_hz = free_running_hz;
    clock->clock_stepped_pio_hz = clock_stepped_pio_hz;
    clock->mode = RP86_EXECUTION_CLOCK_STOPPED;
    return prepare_clock_stepped(clock);
}

bool rp86_execution_clock_set_mode(rp86_execution_clock_t *clock,
                                   rp86_execution_clock_mode_t mode,
                                   uint32_t timeout_us) {
    if (clock == NULL) return false;
    if (clock->mode == mode) return true;
    if (mode == RP86_EXECUTION_CLOCK_CLOCK_STEPPED) {
        if (clock->mode == RP86_EXECUTION_CLOCK_FREE_RUNNING &&
            !stop_free_running_low(clock, timeout_us))
            return false;
        return prepare_clock_stepped(clock);
    }
    if (mode == RP86_EXECUTION_CLOCK_FREE_RUNNING) {
        if (clock->mode != RP86_EXECUTION_CLOCK_CLOCK_STEPPED) return false;
        return prepare_free_running(clock);
    }
    return false;
}

bool rp86_execution_clock_step(rp86_execution_clock_t *clock) {
    if (clock == NULL ||
        clock->mode != RP86_EXECUTION_CLOCK_CLOCK_STEPPED)
        return false;
    const uint64_t deadline = time_us_64() + CLOCK_STEP_TIMEOUT_US;
    while (pio_sm_is_tx_fifo_full(clock->pio, clock->sm)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    pio_sm_put(clock->pio, clock->sm, 1u);
    while (pio_sm_is_rx_fifo_empty(clock->pio, clock->sm)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    (void)pio_sm_get(clock->pio, clock->sm);
    return !gpio_get(RP86_PROCESSOR_PIN_CLK);
}

void rp86_execution_clock_stop_low(rp86_execution_clock_t *clock,
                                   uint32_t timeout_us) {
    if (clock == NULL) return;
    if (clock->mode == RP86_EXECUTION_CLOCK_FREE_RUNNING)
        (void)stop_free_running_low(clock, timeout_us);
    else
        pio_sm_set_enabled(clock->pio, clock->sm, false);
    force_clock_low();
    clock->mode = RP86_EXECUTION_CLOCK_STOPPED;
}

const char *rp86_execution_clock_mode_name(rp86_execution_clock_mode_t mode) {
    switch (mode) {
        case RP86_EXECUTION_CLOCK_FREE_RUNNING: return "FREE_RUNNING";
        case RP86_EXECUTION_CLOCK_CLOCK_STEPPED: return "CLOCK_STEPPED";
        case RP86_EXECUTION_CLOCK_STOPPED: return "STOPPED";
        default: return "UNKNOWN";
    }
}
