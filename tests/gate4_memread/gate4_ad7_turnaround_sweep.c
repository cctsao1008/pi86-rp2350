#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate4_step_clock.pio.h"

#define STEP_PIO_CLOCK_HZ 2000000u
#define PI86_RESET_CLOCKS 8u
#define PI86_MAX_IDLE_STEPS 64u
#define RESET_VECTOR 0xFFFF0u

static const uint32_t turnaround_us[] = {0u, 1u, 2u, 5u, 10u, 20u, 50u};

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0; bit < 16u; ++bit) {
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
    }
    return value;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xfffffu;
}

static void configure_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void drive_cpu_input(uint gpio, bool level) {
    gpio_init(gpio);
    gpio_disable_pulls(gpio);
    gpio_put(gpio, level);
    gpio_set_dir(gpio, GPIO_OUT);
}

static void release_ad_bus(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static void init_step_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_step_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);
    sm_config_set_clkdiv(
        &c,
        (float)clock_get_hz(clk_sys) / (float)STEP_PIO_CLOCK_HZ);
    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static uint32_t pi86_clk(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 1u);
    (void)pio_sm_get_blocking(pio, sm);
    return sio_hw->gpio_in;
}

static void reset_cpu(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }
}

static bool reach_first_read_t2(PIO pio,
                                uint sm,
                                uint32_t *steps_out,
                                uint32_t *t1_out,
                                uint32_t *control_out) {
    drive_cpu_input(V30_PIN_RESET, false);

    uint32_t t1 = 0u;
    uint steps = 0u;
    bool saw_ale = false;

    for (; steps < PI86_MAX_IDLE_STEPS; ++steps) {
        t1 = pi86_clk(pio, sm);
        if (sample_bit(t1, V30_PIN_ALE)) {
            saw_ale = true;
            break;
        }
    }

    if (!saw_ale) {
        *steps_out = steps;
        return false;
    }

    const uint32_t control = pi86_clk(pio, sm);
    *steps_out = steps + 1u;
    *t1_out = t1;
    *control_out = control;

    const uint32_t address = decode_address(t1);
    const uint32_t a0 = sample_bit(t1, V30_PIN_AD0);
    const uint32_t bhe = sample_bit(t1, V30_PIN_BHE);
    const uint32_t iom = sample_bit(control, V30_PIN_IOM);
    const uint32_t dtr = sample_bit(control, V30_PIN_DTR);
    const uint32_t inta = sample_bit(control, V30_PIN_INTA);

    return address == RESET_VECTOR &&
           a0 == 0u && bhe == 0u &&
           iom == 1u && dtr == 0u && inta == 1u;
}

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

int main(void) {
    configure_header_high_z();
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    sleep_ms(100);

    printf("\nGate 4 AD7 T2-turnaround delay sweep\n");
    printf("Purpose: determine whether GPIO9/AD7 is still being held LOW immediately after the pi86-style T2 step.\n");
    printf("Each trial resets the V30, reaches the first aligned FFFF0 memory read, stalls CLK LOW, waits, then drives only AD7 HIGH.\n\n");
    fflush(stdout);

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    bool any_high = false;

    printf(" delay_us  prePAD  OUT  OE  PAD  verdict\n");
    printf(" --------  ------  ---  --  ---  -------\n");

    for (uint i = 0; i < sizeof(turnaround_us) / sizeof(turnaround_us[0]); ++i) {
        const uint32_t delay_us = turnaround_us[i];
        reset_cpu(pio, sm);

        uint32_t steps = 0u;
        uint32_t t1 = 0u;
        uint32_t control = 0u;
        const bool precondition = reach_first_read_t2(pio, sm, &steps, &t1, &control);

        if (!precondition) {
            printf(" %8lu  --      --   --  --   PRECONDITION_FAIL (steps=%lu addr=0x%05lX ctl=%lu/%lu/%lu)\n",
                   (unsigned long)delay_us,
                   (unsigned long)steps,
                   (unsigned long)decode_address(t1),
                   (unsigned long)sample_bit(control, V30_PIN_IOM),
                   (unsigned long)sample_bit(control, V30_PIN_DTR),
                   (unsigned long)sample_bit(control, V30_PIN_INTA));
            continue;
        }

        if (delay_us != 0u) {
            sleep_us(delay_us);
        }

        const uint32_t pre_pad = sample_bit(sio_hw->gpio_in, V30_PIN_AD7);

        sio_hw->gpio_set = 1u << V30_PIN_AD7;
        sio_hw->gpio_oe_set = 1u << V30_PIN_AD7;

        const uint32_t out = sample_bit(sio_hw->gpio_out, V30_PIN_AD7);
        const uint32_t oe = sample_bit(sio_hw->gpio_oe, V30_PIN_AD7);
        const uint32_t pad = sample_bit(sio_hw->gpio_in, V30_PIN_AD7);

        sio_hw->gpio_oe_clr = 1u << V30_PIN_AD7;

        if (pad != 0u) {
            any_high = true;
        }

        printf(" %8lu  %6lu  %3lu  %2lu  %3lu  %s\n",
               (unsigned long)delay_us,
               (unsigned long)pre_pad,
               (unsigned long)out,
               (unsigned long)oe,
               (unsigned long)pad,
               pad ? "AD7_RELEASED" : "AD7_STILL_LOW");
    }

    reset_cpu(pio, sm);
    stop_clock_low(pio, sm);

    printf("\nRESULT: %s\n",
           any_high
               ? "AD7 becomes driveable after a T2 turnaround delay; timing/turnaround is the leading cause."
               : "AD7 remains LOW through the full delay sweep; investigate persistent electrical loading or incorrect bus ownership assumptions.");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) {
        sleep_ms(1000);
    }
}
