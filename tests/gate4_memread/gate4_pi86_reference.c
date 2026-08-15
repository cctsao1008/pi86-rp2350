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

/*
 * Gate 4 pi86 reference-sequence test.
 *
 * Purpose:
 *   Reproduce the early pi86 V30 execution cadence as closely as practical:
 *
 *     RESET=1
 *     CLK() x 8
 *     RESET=0
 *
 *     repeat:
 *       CLK()
 *       sample ALE
 *       if ALE:
 *         latch address/bank
 *         CLK()
 *         sample control
 *         service one aligned memory read
 *         CLK(); CLK();
 *
 * This target intentionally does not use a free-running clock and does not
 * search for ALE by any mechanism other than the one-clock-at-a-time pi86
 * loop above.  A finite loop bound is retained only so a failed experiment
 * returns a diagnostic instead of hanging forever.
 */

#define STEP_PIO_CLOCK_HZ          2000000u
#define PI86_RESET_CLOCKS                 8u
#define PI86_MAX_IDLE_STEPS              64u
#define RESET_VECTOR                0xFFFF0u
#define RESET_VECTOR_WORD            0xFEEBu  /* bytes EB FE */

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
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

static void init_data_luts(void) {
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
}

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
    return address & 0xFFFFFu;
}

static void drive_ad_word(uint16_t word) {
    const uint32_t encoded =
        data_lo_lut[word & 0xFFu] |
        data_hi_lut[(word >> 8) & 0xFFu];

    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = V30_AD_BUS_MASK;
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

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void safe_halt(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }
    stop_clock_low(pio, sm);
}

int main(void) {
    configure_header_high_z();
    init_data_luts();

    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    sleep_ms(100);

    printf("\npi86-rp2350 Gate 4 pi86 reference-sequence test\n");
    printf("Reference cadence: early pi86 V30 Reset()/CLK()/Start_System_Bus().\n");
    printf("Clock remains software-stepped; no free-running clock is used.\n");
    printf("RESET hold clocks = %u (matches early pi86 source).\n", PI86_RESET_CLOCKS);
    printf("Target first address = 0x%05X.\n", RESET_VECTOR);
    printf("Target read data = 0x%04X (bytes EB FE).\n\n", RESET_VECTOR_WORD);
    fflush(stdout);

    PIO pio = pio0;
    const uint clk_sm = pio_claim_unused_sm(pio, true);
    const uint clk_offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, clk_sm, clk_offset);

    /* Exact early-pi86 reset cadence: RESET high for eight CLK() calls. */
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, clk_sm);
    }

    const uint32_t before_release = sio_hw->gpio_in;
    drive_cpu_input(V30_PIN_RESET, false);
    const uint32_t at_release = sio_hw->gpio_in;

    bool saw_ale = false;
    bool address_ok = false;
    bool control_ok = false;
    bool read_cycle_completed = false;

    uint idle_steps = 0u;
    uint32_t t1_sample = 0u;
    uint32_t control_sample = 0u;
    uint32_t data0_sample = 0u;
    uint32_t data1_sample = 0u;
    uint32_t address = 0u;

    /*
     * Early pi86 loop equivalence:
     *   CLK();
     *   if (digitalRead(PIN_ALE) == 1) { ... }
     */
    for (idle_steps = 0u; idle_steps < PI86_MAX_IDLE_STEPS; ++idle_steps) {
        t1_sample = pi86_clk(pio, clk_sm);
        if (sample_bit(t1_sample, V30_PIN_ALE)) {
            saw_ale = true;
            break;
        }
    }

    if (saw_ale) {
        address = decode_address(t1_sample);
        const uint32_t a0 = sample_bit(t1_sample, V30_PIN_AD0);
        const uint32_t bhe = sample_bit(t1_sample, V30_PIN_BHE);
        address_ok = (address == RESET_VECTOR);

        /* Early pi86: after latching address/bank, call CLK() once. */
        control_sample = pi86_clk(pio, clk_sm);

        const uint32_t iom = sample_bit(control_sample, V30_PIN_IOM);
        const uint32_t dtr = sample_bit(control_sample, V30_PIN_DTR);
        const uint32_t inta = sample_bit(control_sample, V30_PIN_INTA);

        /* Aligned 16-bit normal memory read: bank=0, control bus=0x06. */
        control_ok =
            (a0 == 0u) &&
            (bhe == 0u) &&
            (iom == 1u) &&
            (dtr == 0u) &&
            (inta == 1u);

        if (address_ok && control_ok) {
            drive_ad_word(RESET_VECTOR_WORD);

            /* Early pi86 memory-read case advances exactly two more CLK()s. */
            data0_sample = pi86_clk(pio, clk_sm);
            data1_sample = pi86_clk(pio, clk_sm);

            release_ad_bus();
            read_cycle_completed = true;
        }
    }

    safe_halt(pio, clk_sm);

    printf("=== Reference-sequence result ===\n");
    printf("RESET before release             = %lu\n",
           (unsigned long)sample_bit(before_release, V30_PIN_RESET));
    printf("CLK before release               = %lu\n",
           (unsigned long)sample_bit(before_release, V30_PIN_CLK));
    printf("RESET immediately after release  = %lu\n",
           (unsigned long)sample_bit(at_release, V30_PIN_RESET));
    printf("CLK immediately after release    = %lu\n",
           (unsigned long)sample_bit(at_release, V30_PIN_CLK));
    printf("pi86 loop CLK() calls before ALE = %u\n", saw_ale ? idle_steps + 1u : idle_steps);
    printf("PASS A - ALE observed            = %s\n", saw_ale ? "YES" : "NO");

    if (saw_ale) {
        printf("T1 raw                           = 0x%08lX\n",
               (unsigned long)t1_sample);
        printf("ALE / A0 / BHE                   = %lu / %lu / %lu\n",
               (unsigned long)sample_bit(t1_sample, V30_PIN_ALE),
               (unsigned long)sample_bit(t1_sample, V30_PIN_AD0),
               (unsigned long)sample_bit(t1_sample, V30_PIN_BHE));
        printf("decoded address                  = 0x%05lX\n",
               (unsigned long)address);
        printf("PASS B - first address FFFF0     = %s\n", address_ok ? "YES" : "NO");

        printf("control raw                      = 0x%08lX\n",
               (unsigned long)control_sample);
        printf("IO/M / DT/R / INTA               = %lu / %lu / %lu\n",
               (unsigned long)sample_bit(control_sample, V30_PIN_IOM),
               (unsigned long)sample_bit(control_sample, V30_PIN_DTR),
               (unsigned long)sample_bit(control_sample, V30_PIN_INTA));
        printf("PASS C - aligned memory read     = %s\n", control_ok ? "YES" : "NO");
    }

    if (read_cycle_completed) {
        printf("AD after data CLK #1             = 0x%04X\n", decode_ad(data0_sample));
        printf("AD after data CLK #2             = 0x%04X\n", decode_ad(data1_sample));
    }

    printf("PASS D - one FFFF0 read serviced = %s\n",
           read_cycle_completed ? "YES" : "NO");

    const bool pass =
        saw_ale && address_ok && control_ok && read_cycle_completed;

    printf("\nGATE 4 PI86 REFERENCE RESULT: %s\n", pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0u;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 pi86-reference heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
