#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate4_step_clock.pio.h"

#define GATE4_CPU_CLOCK_HZ          100000u
#define GATE4_PIO_CLOCK_HZ         2000000u
#define GATE4_RESET_CLOCKS              20u
#define GATE4_ALE_SEARCH_CLOCKS         96u
#define GATE4_PAD_SETTLE_US               2u
#define GATE4_RESET_VECTOR          0xFFFF0u

static const uint16_t matrix_patterns[] = {
    0x0000u,
    0xFFFFu,
    0xAAAAu,
    0x5555u,
    0x0080u,
    0xFF7Fu,
    0xFEEBu,
};

static const uint32_t matrix_turnaround_us[] = {
    0u, 1u, 2u, 5u, 10u, 20u,
};

#define MATRIX_PATTERN_COUNT (sizeof(matrix_patterns) / sizeof(matrix_patterns[0]))
#define MATRIX_DELAY_COUNT (sizeof(matrix_turnaround_us) / sizeof(matrix_turnaround_us[0]))
#define MATRIX_TRIAL_COUNT (MATRIX_PATTERN_COUNT * MATRIX_DELAY_COUNT)

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];

typedef enum {
    MATRIX_TRIAL_OK = 0,
    MATRIX_TRIAL_NO_RESET_VECTOR,
    MATRIX_TRIAL_UNSUPPORTED_LANE,
    MATRIX_TRIAL_NOT_MEMORY_READ,
} matrix_trial_status_t;

typedef struct {
    uint16_t pattern;
    uint32_t turnaround_us;
    matrix_trial_status_t status;

    uint32_t reset_sample;
    uint32_t t1_sample;
    uint32_t control_sample;
    uint32_t out_sample;
    uint32_t oe_sample;
    uint32_t after_oe_sample;
    uint32_t settled_sample;
    uint32_t data_clock1_sample;
    uint32_t data_clock2_sample;

    uint32_t address;
    uint16_t out_readback;
    uint16_t after_oe_readback;
    uint16_t settled_readback;
    uint16_t data_clock1_readback;
    uint16_t data_clock2_readback;

    uint8_t a0;
    uint8_t bhe;
    uint8_t iom;
    uint8_t dtr;
    uint8_t inta;
    uint8_t search_steps;
} matrix_trial_t;

static matrix_trial_t matrix_results[MATRIX_TRIAL_COUNT];

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

static void init_data_luts(void) {
    for (uint32_t value = 0; value < 256u; ++value) {
        uint32_t lo_mask = 0;
        uint32_t hi_mask = 0;

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
    uint16_t value = 0;
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

static void release_ad_bus(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
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

    const float divider =
        (float)clock_get_hz(clk_sys) / (float)GATE4_PIO_CLOCK_HZ;
    sm_config_set_clkdiv(&c, divider);

    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static uint32_t clock_step(PIO pio, uint sm) {
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

static uint32_t assert_reset_and_clock(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);

    uint32_t sample = sio_hw->gpio_in;
    for (uint i = 0; i < GATE4_RESET_CLOCKS; ++i) {
        sample = clock_step(pio, sm);
    }
    return sample;
}

static const char *status_string(matrix_trial_status_t status) {
    switch (status) {
        case MATRIX_TRIAL_OK:
            return "OK";
        case MATRIX_TRIAL_NO_RESET_VECTOR:
            return "NO_FFFF0";
        case MATRIX_TRIAL_UNSUPPORTED_LANE:
            return "BAD_LANE";
        case MATRIX_TRIAL_NOT_MEMORY_READ:
            return "BAD_CTL";
        default:
            return "UNKNOWN";
    }
}

static bool capture_first_reset_vector_t1(PIO pio,
                                          uint sm,
                                          matrix_trial_t *result) {
    /*
     * Gate-3-style policy: reset-time ALE is informational only. After the
     * HIGH->LOW RESET release, search post-release bus cycles until an ALE-high
     * sample actually decodes to 0xFFFF0. This rejects stale ALE-high levels
     * without requiring ALE to become LOW while RESET is asserted.
     */
    for (uint step = 0; step < GATE4_ALE_SEARCH_CLOCKS; ++step) {
        const uint32_t sample = clock_step(pio, sm);
        if (sample_bit(sample, V30_PIN_ALE) != 0u &&
            decode_address(sample) == GATE4_RESET_VECTOR) {
            result->t1_sample = sample;
            result->address = GATE4_RESET_VECTOR;
            result->search_steps = (uint8_t)(step + 1u);
            return true;
        }
    }
    return false;
}

static void run_matrix_trial(PIO pio,
                             uint sm,
                             uint16_t pattern,
                             uint32_t turnaround_us,
                             matrix_trial_t *result) {
    *result = (matrix_trial_t){0};
    result->pattern = pattern;
    result->turnaround_us = turnaround_us;
    result->status = MATRIX_TRIAL_NO_RESET_VECTOR;

    /* CPU enters each trial already held in RESET from the previous trial. */
    result->reset_sample = assert_reset_and_clock(pio, sm);
    drive_cpu_input(V30_PIN_RESET, false);

    if (!capture_first_reset_vector_t1(pio, sm, result)) {
        goto done;
    }

    result->a0 = (uint8_t)sample_bit(result->t1_sample, V30_PIN_AD0);
    result->bhe = (uint8_t)sample_bit(result->t1_sample, V30_PIN_BHE);
    if (result->a0 != 0u || result->bhe != 0u) {
        result->status = MATRIX_TRIAL_UNSUPPORTED_LANE;
        goto done;
    }

    result->control_sample = clock_step(pio, sm);
    result->iom = (uint8_t)sample_bit(result->control_sample, V30_PIN_IOM);
    result->dtr = (uint8_t)sample_bit(result->control_sample, V30_PIN_DTR);
    result->inta = (uint8_t)sample_bit(result->control_sample, V30_PIN_INTA);

    if (result->iom != 1u || result->dtr != 0u || result->inta != 1u) {
        result->status = MATRIX_TRIAL_NOT_MEMORY_READ;
        goto done;
    }

    if (turnaround_us != 0u) {
        busy_wait_us_32(turnaround_us);
    }

    drive_ad_word(pattern);
    result->out_sample = sio_hw->gpio_out;
    result->oe_sample = sio_hw->gpio_oe;
    result->out_readback = decode_ad(result->out_sample);

    result->after_oe_sample = sio_hw->gpio_in;
    result->after_oe_readback = decode_ad(result->after_oe_sample);

    busy_wait_us_32(GATE4_PAD_SETTLE_US);
    result->settled_sample = sio_hw->gpio_in;
    result->settled_readback = decode_ad(result->settled_sample);

    result->data_clock1_sample = clock_step(pio, sm);
    result->data_clock1_readback = decode_ad(result->data_clock1_sample);

    result->data_clock2_sample = clock_step(pio, sm);
    result->data_clock2_readback = decode_ad(result->data_clock2_sample);

    result->status = MATRIX_TRIAL_OK;

done:
    release_ad_bus();
    (void)assert_reset_and_clock(pio, sm);
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 4 reset-vector turnaround/pattern matrix v3\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("HAT: original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("CPU: NEC V30 D70116C-8\n\n");
    printf("Clock pulse: nominal %u Hz, host-stepped with CLK stalled LOW.\n",
           GATE4_CPU_CLOCK_HZ);
    printf("Every trial: RESET=HIGH for %u clocks, RESET release, then search\n",
           GATE4_RESET_CLOCKS);
    printf("post-release ALE-high samples until 0xFFFF0 is observed.\n");
    printf("Reset-time ALE is diagnostic only; no ALE-low reset requirement.\n");
    printf("Turnaround delays: 0, 1, 2, 5, 10, 20 us.\n");
    printf("Patterns: 0000 FFFF AAAA 5555 0080 FF7F FEEB.\n");
    printf("No PSRAM is used.\n\n");
    fflush(stdout);
}

static void print_matrix(void) {
    printf("\nMatrix columns:\n");
    printf("  PAT  DLY ADDR STP R OUT   OE PAD0 PAD2 CLK1 CLK2 AD7[O/E/P2/C1] STATUS\n");
    printf("  ---- --- ----- --- - ---- ---- ---- ---- ---- ---- ------------- --------\n");

    uint setup_ok = 0;
    uint out_ok = 0;
    uint oe_ok = 0;
    uint pad2_ok = 0;
    uint clk1_ok = 0;
    uint clk2_ok = 0;
    uint16_t pad2_mismatch_or = 0;
    uint16_t clk1_mismatch_or = 0;
    uint16_t clk2_mismatch_or = 0;

    for (uint i = 0; i < MATRIX_TRIAL_COUNT; ++i) {
        const matrix_trial_t *r = &matrix_results[i];
        const bool trial_ok = r->status == MATRIX_TRIAL_OK;
        const bool out_match = trial_ok && r->out_readback == r->pattern;
        const bool oe_match = trial_ok &&
            ((r->oe_sample & V30_AD_BUS_MASK) == V30_AD_BUS_MASK);
        const bool pad2_match = trial_ok && r->settled_readback == r->pattern;
        const bool clk1_match = trial_ok && r->data_clock1_readback == r->pattern;
        const bool clk2_match = trial_ok && r->data_clock2_readback == r->pattern;

        setup_ok += trial_ok ? 1u : 0u;
        out_ok += out_match ? 1u : 0u;
        oe_ok += oe_match ? 1u : 0u;
        pad2_ok += pad2_match ? 1u : 0u;
        clk1_ok += clk1_match ? 1u : 0u;
        clk2_ok += clk2_match ? 1u : 0u;

        if (trial_ok) {
            pad2_mismatch_or |= (uint16_t)(r->settled_readback ^ r->pattern);
            clk1_mismatch_or |= (uint16_t)(r->data_clock1_readback ^ r->pattern);
            clk2_mismatch_or |= (uint16_t)(r->data_clock2_readback ^ r->pattern);
        }

        printf("  %04X %3lu %05lX %3u %lu %04X  %c  %04X %04X %04X %04X    %lu/%lu/%lu/%lu   %s\n",
               r->pattern,
               (unsigned long)r->turnaround_us,
               (unsigned long)r->address,
               r->search_steps,
               (unsigned long)sample_bit(r->reset_sample, V30_PIN_ALE),
               r->out_readback,
               oe_match ? 'Y' : 'N',
               r->after_oe_readback,
               r->settled_readback,
               r->data_clock1_readback,
               r->data_clock2_readback,
               (unsigned long)sample_bit(r->out_sample, V30_PIN_AD7),
               (unsigned long)sample_bit(r->oe_sample, V30_PIN_AD7),
               (unsigned long)sample_bit(r->settled_sample, V30_PIN_AD7),
               (unsigned long)sample_bit(r->data_clock1_sample, V30_PIN_AD7),
               status_string(r->status));
    }

    printf("\nSummary (%u trials):\n", (unsigned)MATRIX_TRIAL_COUNT);
    printf("  first-cycle setup valid = %u\n", setup_ok);
    printf("  SIO OUT matches pattern = %u\n", out_ok);
    printf("  AD OE mask fully enabled= %u\n", oe_ok);
    printf("  pad match after %u us   = %u\n", GATE4_PAD_SETTLE_US, pad2_ok);
    printf("  pad match after clk #1  = %u\n", clk1_ok);
    printf("  pad match after clk #2  = %u\n", clk2_ok);
    printf("  pad2 mismatch OR mask   = 0x%04X\n", pad2_mismatch_or);
    printf("  clk1 mismatch OR mask   = 0x%04X\n", clk1_mismatch_or);
    printf("  clk2 mismatch OR mask   = 0x%04X\n", clk2_mismatch_or);

    printf("\nPer-delay match summary (each delay has %u patterns):\n",
           (unsigned)MATRIX_PATTERN_COUNT);
    printf("  DLY SETUP OUT OE PAD2 CLK1 CLK2\n");
    for (uint d = 0; d < MATRIX_DELAY_COUNT; ++d) {
        uint setup = 0;
        uint out = 0;
        uint oe = 0;
        uint pad2 = 0;
        uint clk1 = 0;
        uint clk2 = 0;

        for (uint p = 0; p < MATRIX_PATTERN_COUNT; ++p) {
            const matrix_trial_t *r =
                &matrix_results[p * MATRIX_DELAY_COUNT + d];
            const bool trial_ok = r->status == MATRIX_TRIAL_OK;
            setup += trial_ok ? 1u : 0u;
            out += (trial_ok && r->out_readback == r->pattern) ? 1u : 0u;
            oe += (trial_ok &&
                   ((r->oe_sample & V30_AD_BUS_MASK) == V30_AD_BUS_MASK)) ? 1u : 0u;
            pad2 += (trial_ok && r->settled_readback == r->pattern) ? 1u : 0u;
            clk1 += (trial_ok && r->data_clock1_readback == r->pattern) ? 1u : 0u;
            clk2 += (trial_ok && r->data_clock2_readback == r->pattern) ? 1u : 0u;
        }

        printf("  %3lu   %u   %u  %u   %u    %u    %u\n",
               (unsigned long)matrix_turnaround_us[d],
               setup, out, oe, pad2, clk1, clk2);
    }

    printf("\nMATRIX RESULT: %s\n",
           setup_ok == MATRIX_TRIAL_COUNT ? "COMPLETE" : "INCOMPLETE");
    printf("This diagnostic does not by itself declare Gate 4 PASS.\n");
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

    print_banner();
    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Matrix starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO pio = pio0;
    const uint clk_sm = pio_claim_unused_sm(pio, true);
    const uint clk_offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, clk_sm, clk_offset);

    (void)assert_reset_and_clock(pio, clk_sm);

    uint result_index = 0;
    for (uint p = 0; p < MATRIX_PATTERN_COUNT; ++p) {
        for (uint d = 0; d < MATRIX_DELAY_COUNT; ++d) {
            run_matrix_trial(pio,
                             clk_sm,
                             matrix_patterns[p],
                             matrix_turnaround_us[d],
                             &matrix_results[result_index++]);
        }
    }

    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    stop_clock_low(pio, clk_sm);

    print_matrix();
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 matrix v3 heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
