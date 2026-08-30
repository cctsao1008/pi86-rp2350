#include "bus/processor_bus.h"

#include <stddef.h>

#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "bus/processor_bus_pins.h"

static const uint8_t ad_gpio[16] = {
    RP86_PROCESSOR_PIN_AD0, RP86_PROCESSOR_PIN_AD1, RP86_PROCESSOR_PIN_AD2, RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4, RP86_PROCESSOR_PIN_AD5, RP86_PROCESSOR_PIN_AD6, RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8, RP86_PROCESSOR_PIN_AD9, RP86_PROCESSOR_PIN_AD10, RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12, RP86_PROCESSOR_PIN_AD13, RP86_PROCESSOR_PIN_AD14, RP86_PROCESSOR_PIN_AD15,
};

static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];
static bool data_luts_ready;

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static uint32_t low_lane_mask(void) {
    uint32_t mask = 0u;
    for (uint bit = 0; bit < 8u; ++bit) mask |= 1u << ad_gpio[bit];
    return mask;
}

static uint32_t high_lane_mask(void) {
    uint32_t mask = 0u;
    for (uint bit = 8u; bit < 16u; ++bit) mask |= 1u << ad_gpio[bit];
    return mask;
}

static void init_data_luts(void) {
    if (data_luts_ready) return;

    for (uint32_t value = 0; value < 256u; ++value) {
        uint32_t lo_mask = 0u;
        uint32_t hi_mask = 0u;
        for (uint bit = 0; bit < 8u; ++bit) {
            if ((value >> bit) & 1u) {
                lo_mask |= 1u << ad_gpio[bit];
                hi_mask |= 1u << ad_gpio[bit + 8u];
            }
        }
        data_lo_lut[value] = lo_mask;
        data_hi_lut[value] = hi_mask;
    }

    data_luts_ready = true;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = rp86_processor_bus_decode_ad(sample);
    address |= sample_bit(sample, RP86_PROCESSOR_PIN_A16) << 16;
    address |= sample_bit(sample, RP86_PROCESSOR_PIN_A17) << 17;
    address |= sample_bit(sample, RP86_PROCESSOR_PIN_A18) << 18;
    address |= sample_bit(sample, RP86_PROCESSOR_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static rp86_processor_bus_lanes_t decode_lanes(uint8_t a0, uint8_t bhe_n) {
    if (a0 == 0u && bhe_n == 0u) return RP86_PROCESSOR_BUS_LANES_WORD;
    if (a0 == 0u && bhe_n == 1u) return RP86_PROCESSOR_BUS_LANE_LOW;
    if (a0 == 1u && bhe_n == 0u) return RP86_PROCESSOR_BUS_LANE_HIGH;
    return RP86_PROCESSOR_BUS_LANES_NONE;
}

static rp86_processor_bus_cycle_type_t decode_cycle_type(uint8_t iom,
                                               uint8_t dtr,
                                               uint8_t inta_n) {
    if (inta_n == 0u) return RP86_PROCESSOR_BUS_CYCLE_INTERRUPT_ACK;
    if (iom != 0u) {
        return dtr == 0u ? RP86_PROCESSOR_BUS_CYCLE_MEM_READ : RP86_PROCESSOR_BUS_CYCLE_MEM_WRITE;
    }
    return dtr == 0u ? RP86_PROCESSOR_BUS_CYCLE_IO_READ : RP86_PROCESSOR_BUS_CYCLE_IO_WRITE;
}

void rp86_processor_bus_prepare_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

void rp86_processor_bus_hold_reset(bool asserted) {
    gpio_init(RP86_PROCESSOR_PIN_RESET);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_RESET);
    gpio_put(RP86_PROCESSOR_PIN_RESET, asserted);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);
}

void rp86_processor_bus_set_intr(bool asserted) {
    gpio_init(RP86_PROCESSOR_PIN_INTR);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_INTR);
    gpio_put(RP86_PROCESSOR_PIN_INTR, asserted);
    gpio_set_dir(RP86_PROCESSOR_PIN_INTR, GPIO_OUT);
}

void rp86_processor_bus_release_ad(void) {
    sio_hw->gpio_oe_clr = RP86_PROCESSOR_AD_BUS_MASK;
}

bool rp86_processor_bus_init(rp86_processor_bus_t *bus, PIO pio,
                             uint32_t free_running_hz,
                             uint32_t clock_stepped_pio_hz) {
    if (bus == NULL) return false;
    init_data_luts();
    bus->faulted = false;
    bus->last_step_us = 0u;
    bus->max_step_interval_us = 0u;
    return rp86_execution_clock_init(&bus->execution_clock, pio,
                                     free_running_hz,
                                     clock_stepped_pio_hz);
}

void rp86_processor_bus_force_safe_state(rp86_processor_bus_t *bus) {
    rp86_processor_bus_release_ad();
    rp86_processor_bus_hold_reset(true);
    if (bus != NULL)
        rp86_execution_clock_stop_low(&bus->execution_clock, 100000u);
    gpio_init(RP86_PROCESSOR_PIN_CLK);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_CLK);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);
}

uint32_t rp86_processor_bus_step(rp86_processor_bus_t *bus) {
    if (bus != NULL) {
        const uint64_t now = time_us_64();
        if (bus->last_step_us != 0u) {
            const uint64_t interval = now - bus->last_step_us;
            const uint32_t bounded = interval > UINT32_MAX ?
                UINT32_MAX : (uint32_t)interval;
            if (bounded > bus->max_step_interval_us)
                bus->max_step_interval_us = bounded;
        }
        bus->last_step_us = now;
    }
    if (bus == NULL || !rp86_execution_clock_step(&bus->execution_clock)) {
        if (bus != NULL) bus->faulted = true;
        rp86_processor_bus_force_safe_state(bus);
    }
    return sio_hw->gpio_in;
}

bool rp86_processor_bus_set_execution_clock_mode(
    rp86_processor_bus_t *bus, rp86_execution_clock_mode_t mode,
    uint32_t timeout_us) {
    return bus != NULL && rp86_execution_clock_set_mode(
        &bus->execution_clock, mode, timeout_us);
}

rp86_execution_clock_mode_t rp86_processor_bus_execution_clock_mode(
    const rp86_processor_bus_t *bus) {
    return bus == NULL ? RP86_EXECUTION_CLOCK_STOPPED :
        bus->execution_clock.mode;
}

bool rp86_processor_bus_faulted(const rp86_processor_bus_t *bus) {
    return bus == NULL || bus->faulted;
}

void rp86_processor_bus_clear_fault(rp86_processor_bus_t *bus) {
    if (bus != NULL) bus->faulted = false;
}

void rp86_processor_bus_reset_step_timing(rp86_processor_bus_t *bus) {
    if (bus == NULL) return;
    bus->last_step_us = 0u;
    bus->max_step_interval_us = 0u;
}

uint32_t rp86_processor_bus_max_step_interval_us(
    const rp86_processor_bus_t *bus) {
    return bus == NULL ? 0u : bus->max_step_interval_us;
}

bool rp86_processor_bus_reset_sequence(rp86_processor_bus_t *bus, uint reset_clocks) {
    if (bus == NULL) return false;
    rp86_processor_bus_clear_fault(bus);
    rp86_processor_bus_release_ad();
    rp86_processor_bus_hold_reset(true);
    for (uint i = 0; i < reset_clocks; ++i) {
        (void)rp86_processor_bus_step(bus);
        if (rp86_processor_bus_faulted(bus)) return false;
    }
    rp86_processor_bus_hold_reset(false);
    return true;
}

bool rp86_processor_bus_wait_cycle(rp86_processor_bus_t *bus,
                        uint max_idle_steps,
                        rp86_processor_bus_cycle_t *cycle) {
    uint32_t t1 = 0u;
    uint idle_steps = 0u;

    for (; idle_steps < max_idle_steps; ++idle_steps) {
        t1 = rp86_processor_bus_step(bus);
        if (rp86_processor_bus_faulted(bus)) return false;
        if (sample_bit(t1, RP86_PROCESSOR_PIN_ALE)) break;
    }

    if (idle_steps == max_idle_steps) return false;

    cycle->t1_sample = t1;
    cycle->address = decode_address(t1);
    cycle->a0 = (uint8_t)sample_bit(t1, RP86_PROCESSOR_PIN_AD0);
    cycle->bhe_n = (uint8_t)sample_bit(t1, RP86_PROCESSOR_PIN_BHE);
    cycle->idle_steps = idle_steps + 1u;
    cycle->lanes = decode_lanes(cycle->a0, cycle->bhe_n);

    cycle->control_sample = rp86_processor_bus_step(bus);
    if (rp86_processor_bus_faulted(bus)) return false;
    cycle->iom = (uint8_t)sample_bit(cycle->control_sample, RP86_PROCESSOR_PIN_IOM);
    cycle->dtr = (uint8_t)sample_bit(cycle->control_sample, RP86_PROCESSOR_PIN_DTR);
    cycle->inta_n = (uint8_t)sample_bit(cycle->control_sample, RP86_PROCESSOR_PIN_INTA);
    cycle->type = decode_cycle_type(cycle->iom, cycle->dtr, cycle->inta_n);

    if (cycle->lanes == RP86_PROCESSOR_BUS_LANES_NONE) cycle->type = RP86_PROCESSOR_BUS_CYCLE_UNSUPPORTED;
    return true;
}

uint16_t rp86_processor_bus_decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0; bit < 16u; ++bit) {
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
    }
    return value;
}

void rp86_processor_bus_drive_data(uint16_t value, rp86_processor_bus_lanes_t lanes) {
    uint32_t encoded = 0u;
    uint32_t oe_mask = 0u;

    if ((lanes & RP86_PROCESSOR_BUS_LANE_LOW) != 0u) {
        encoded |= data_lo_lut[value & 0xFFu];
        oe_mask |= low_lane_mask();
    }
    if ((lanes & RP86_PROCESSOR_BUS_LANE_HIGH) != 0u) {
        encoded |= data_hi_lut[(value >> 8) & 0xFFu];
        oe_mask |= high_lane_mask();
    }

    sio_hw->gpio_clr = RP86_PROCESSOR_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = oe_mask;
}

bool rp86_processor_bus_complete_read(rp86_processor_bus_t *bus,
                           uint16_t *readback1,
                           uint16_t *readback2) {
    const uint32_t sample1 = rp86_processor_bus_step(bus);
    if (rp86_processor_bus_faulted(bus)) return false;
    const uint32_t sample2 = rp86_processor_bus_step(bus);
    if (rp86_processor_bus_faulted(bus)) return false;
    if (readback1 != NULL) *readback1 = rp86_processor_bus_decode_ad(sample1);
    if (readback2 != NULL) *readback2 = rp86_processor_bus_decode_ad(sample2);
    rp86_processor_bus_release_ad();
    return true;
}

bool rp86_processor_bus_complete_write(rp86_processor_bus_t *bus,
                            const rp86_processor_bus_cycle_t *cycle,
                            uint16_t *sample0,
                            uint16_t *sample1,
                            uint16_t *sample2) {
    rp86_processor_bus_release_ad();

    const uint16_t d0 = rp86_processor_bus_decode_ad(cycle->control_sample);
    const uint16_t d1 = rp86_processor_bus_decode_ad(rp86_processor_bus_step(bus));
    if (rp86_processor_bus_faulted(bus)) return false;
    const uint16_t d2 = rp86_processor_bus_decode_ad(rp86_processor_bus_step(bus));
    if (rp86_processor_bus_faulted(bus)) return false;

    if (sample0 != NULL) *sample0 = d0;
    if (sample1 != NULL) *sample1 = d1;
    if (sample2 != NULL) *sample2 = d2;
    return true;
}

void rp86_processor_bus_safe_halt(rp86_processor_bus_t *bus, uint reset_clocks) {
    rp86_processor_bus_release_ad();
    if (!rp86_processor_bus_set_execution_clock_mode(
            bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED, 100000u)) {
        if (bus != NULL) bus->faulted = true;
        rp86_processor_bus_force_safe_state(bus);
        return;
    }
    rp86_processor_bus_clear_fault(bus);
    rp86_processor_bus_hold_reset(true);
    for (uint i = 0; i < reset_clocks; ++i) {
        (void)rp86_processor_bus_step(bus);
        if (rp86_processor_bus_faulted(bus)) break;
    }

    rp86_processor_bus_force_safe_state(bus);
}
