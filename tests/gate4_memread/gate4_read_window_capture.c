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
#include "gate4_read_window_capture.pio.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define CAPTURE_PIO_CLOCK_HZ    8000000u
#define RESET_CLOCKS                 20u
#define TOTAL_STEP_CLOCKS            18u
#define CAPTURE_SAMPLES              96u
#define RESET_VECTOR            0xFFFF0u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t samples[CAPTURE_SAMPLES];
static uint sample_count;

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
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
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (float)STEP_PIO_CLOCK_HZ);

    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_capture(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_read_window_capture_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (float)CAPTURE_PIO_CLOCK_HZ);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void drain_capture(PIO pio, uint sm) {
    while (!pio_sm_is_rx_fifo_empty(pio, sm) && sample_count < CAPTURE_SAMPLES) {
        /* PIO shift-right places GPIO0..GPIO27 in bits 31..4. */
        samples[sample_count++] = pio_sm_get(pio, sm) >> 4;
    }
}

static void step_clock_and_drain(PIO step_pio,
                                 uint step_sm,
                                 PIO cap_pio,
                                 uint cap_sm) {
    pio_sm_put_blocking(step_pio, step_sm, 1u);

    while (pio_sm_is_rx_fifo_empty(step_pio, step_sm)) {
        drain_capture(cap_pio, cap_sm);
        tight_loop_contents();
    }

    (void)pio_sm_get(step_pio, step_sm);
    drain_capture(cap_pio, cap_sm);
}

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void print_samples(void) {
    printf("\n=== Passive first-read window capture ===\n");
    printf("Sampling starts on first post-RESET ALE=1. No AD GPIO is driven.\n");
    printf("Nominal sample spacing is about 0.5 us; chunk boundaries add a small gap.\n\n");
    printf(" IDX  us   CLK ALE IOM DTR BHE INTA  AD     A19:16 ADDR   AD4 AD7 AD6 AD8  MARK\n");
    printf(" --- ----- --- --- --- --- --- ---- ------ ------- -----  --- --- --- ---  --------\n");

    bool saw_reset_vector = false;
    for (uint i = 0; i < sample_count; ++i) {
        const uint32_t s = samples[i];
        const uint16_t ad = decode_ad(s);
        const uint32_t a19_16 =
            (sample_bit(s, V30_PIN_A16) << 0) |
            (sample_bit(s, V30_PIN_A17) << 1) |
            (sample_bit(s, V30_PIN_A18) << 2) |
            (sample_bit(s, V30_PIN_A19) << 3);
        const uint32_t addr = decode_address(s);
        const bool reset_t1 =
            sample_bit(s, V30_PIN_ALE) != 0u && addr == RESET_VECTOR;

        if (reset_t1) {
            saw_reset_vector = true;
        }

        printf(" %3u %5.1f  %u   %u   %u   %u   %u    %u   %04X     %lX   %05lX   %u   %u   %u   %u  %s\n",
               i,
               (double)i * 0.5,
               (unsigned)sample_bit(s, V30_PIN_CLK),
               (unsigned)sample_bit(s, V30_PIN_ALE),
               (unsigned)sample_bit(s, V30_PIN_IOM),
               (unsigned)sample_bit(s, V30_PIN_DTR),
               (unsigned)sample_bit(s, V30_PIN_BHE),
               (unsigned)sample_bit(s, V30_PIN_INTA),
               ad,
               (unsigned long)a19_16,
               (unsigned long)addr,
               (unsigned)sample_bit(s, V30_PIN_AD4),
               (unsigned)sample_bit(s, V30_PIN_AD7),
               (unsigned)sample_bit(s, V30_PIN_AD6),
               (unsigned)sample_bit(s, V30_PIN_AD8),
               reset_t1 ? "FFFF0-T1" : "");
    }

    printf("\nCapture summary:\n");
    printf("  samples captured   = %u / %u\n", sample_count, CAPTURE_SAMPLES);
    printf("  observed FFFF0/ALE = %s\n", saw_reset_vector ? "YES" : "NO");
    printf("  AD bus driven by RP2350 during capture = NO\n");
    printf("This diagnostic is passive and does not declare Gate 4 PASS.\n");
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

    printf("\npi86-rp2350 Gate 4 passive read-window capture\n");
    printf("RP2350 PIO samples the first post-RESET ALE window at sub-clock resolution.\n");
    printf("The AD bus remains high-Z for the entire capture; this run only observes V30 outputs.\n");
    printf("Host clock remains nominal 100 kHz using the existing step-clock state machine.\n");
    printf("No PSRAM is used.\n\n");

    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Capture starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO step_pio = pio0;
    const uint step_sm = pio_claim_unused_sm(step_pio, true);
    const uint step_offset = pio_add_program(step_pio, &gate4_step_clk_program);
    init_step_clock(step_pio, step_sm, step_offset);

    PIO cap_pio = pio1;
    const uint cap_sm = pio_claim_unused_sm(cap_pio, true);
    const uint cap_offset = pio_add_program(cap_pio, &gate4_read_window_capture_program);
    init_capture(cap_pio, cap_sm, cap_offset);

    /* Establish a valid RESET hold while the passive sampler waits for release/ALE. */
    for (uint i = 0; i < RESET_CLOCKS; ++i) {
        step_clock_and_drain(step_pio, step_sm, cap_pio, cap_sm);
    }

    /* Clear any impossible pre-release capture data, then release RESET. */
    pio_sm_clear_fifos(cap_pio, cap_sm);
    sample_count = 0;
    drive_cpu_input(V30_PIN_RESET, false);

    /* Back-to-back host steps keep the clock moving while the capture SM drains. */
    for (uint i = 0; i < TOTAL_STEP_CLOCKS; ++i) {
        step_clock_and_drain(step_pio, step_sm, cap_pio, cap_sm);
    }

    /* Drain any final words already present in the RX FIFO. */
    for (uint i = 0; i < 1000u && sample_count < CAPTURE_SAMPLES; ++i) {
        drain_capture(cap_pio, cap_sm);
        if (sample_count >= CAPTURE_SAMPLES) {
            break;
        }
        tight_loop_contents();
    }

    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < RESET_CLOCKS; ++i) {
        step_clock_and_drain(step_pio, step_sm, cap_pio, cap_sm);
    }
    stop_clock_low(step_pio, step_sm);
    pio_sm_set_enabled(cap_pio, cap_sm, false);

    print_samples();
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 read-window heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
