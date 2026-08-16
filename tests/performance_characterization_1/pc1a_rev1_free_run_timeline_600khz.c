/*
 * PC1-A Rev1 free-running raw bus timeline diagnostic at 0.600 MHz.
 *
 * A/B companion to pc1a_rev1_free_run_timeline.c (0.300 MHz).
 * The RP2350 never drives AD15:0.  RESET is held for the same 20 counted
 * V30 clocks and released on a falling-edge boundary.  Raw GPIO transitions
 * are captured to SRAM and printed only after the clock is stopped.
 *
 * Primary comparison against 0.300 MHz:
 *   1. FFFF0 -> FFFF2 -> FFFF4 -> FFFF6 must remain visible.
 *   2. Bus-cycle spacing should be approximately halved.
 *   3. CLK/ALE/control phase relationships should remain qualitatively equal.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "v30/v30_pins.h"
#include "perf_continuous_clock.pio.h"

#define V30_HZ               600000u
#define RESET_CLOCKS         20u
#define SIGNAL_TIMEOUT_CLOCKS 64u
#define TIMELINE_DEPTH       128u
#define TIMELINE_MAX_US      50u

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} perf_clock_t;

typedef struct {
    uint32_t sequence;
    uint32_t sample;
    uint32_t elapsed_us;
} timeline_entry_t;

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static inline uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
    return value;
}

static inline uint8_t decode_high_nibble(uint32_t sample) {
    uint8_t high = 0u;
    high |= (uint8_t)(sample_bit(sample, V30_PIN_A16) << 0);
    high |= (uint8_t)(sample_bit(sample, V30_PIN_A17) << 1);
    high |= (uint8_t)(sample_bit(sample, V30_PIN_A18) << 2);
    high |= (uint8_t)(sample_bit(sample, V30_PIN_A19) << 3);
    return high;
}

static void prepare_header_high_z(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void init_control_outputs(void) {
    gpio_init(V30_PIN_RESET);
    gpio_disable_pulls(V30_PIN_RESET);
    gpio_put(V30_PIN_RESET, true);
    gpio_set_dir(V30_PIN_RESET, GPIO_OUT);

    gpio_init(V30_PIN_INTR);
    gpio_disable_pulls(V30_PIN_INTR);
    gpio_put(V30_PIN_INTR, false);
    gpio_set_dir(V30_PIN_INTR, GPIO_OUT);
}

static inline void hold_reset(bool asserted) {
    gpio_put(V30_PIN_RESET, asserted);
}

static inline void set_intr(bool asserted) {
    gpio_put(V30_PIN_INTR, asserted);
}

static inline void release_ad(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static inline uint64_t timeout_us_from_clocks(uint32_t clocks) {
    uint64_t us = ((uint64_t)clocks * 1000000ull + V30_HZ - 1u) / V30_HZ;
    return us + 2u;
}

static bool __not_in_flash_func(wait_level_until)(uint gpio,
                                                   bool level,
                                                   uint64_t deadline_us) {
    while (time_us_64() <= deadline_us) {
        const uint32_t sample = sio_hw->gpio_in;
        if ((sample_bit(sample, gpio) != 0u) == level)
            return true;
    }
    return false;
}

static bool __not_in_flash_func(wait_falling_edge)(uint64_t timeout_us) {
    uint64_t deadline = time_us_64() + timeout_us;
    if (!wait_level_until(V30_PIN_CLK, true, deadline)) return false;
    deadline = time_us_64() + timeout_us;
    return wait_level_until(V30_PIN_CLK, false, deadline);
}

static bool __not_in_flash_func(wait_reset_clocks)(uint count) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    for (uint i = 0u; i < count; ++i) {
        if (!wait_falling_edge(timeout_us)) return false;
    }
    return true;
}

static void perf_clock_init(perf_clock_t *clock, PIO pio) {
    clock->pio = pio;
    clock->sm = pio_claim_unused_sm(pio, true);
    clock->offset = pio_add_program(pio, &perf_continuous_clk_program);
}

static void perf_clock_start(perf_clock_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);

    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);

    pio_sm_config c = perf_continuous_clk_program_get_default_config(clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)V30_HZ));

    pio_gpio_init(clock->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm, V30_PIN_CLK, 1u, true);
    pio_sm_init(clock->pio, clock->sm, clock->offset, &c);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
}

static void perf_clock_stop(perf_clock_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static inline uint32_t timeline_mask(void) {
    return V30_AD_BUS_MASK |
           (1u << V30_PIN_CLK) |
           (1u << V30_PIN_ALE) |
           (1u << V30_PIN_IOM) |
           (1u << V30_PIN_DTR) |
           (1u << V30_PIN_BHE) |
           (1u << V30_PIN_INTA) |
           (1u << V30_PIN_A16) |
           (1u << V30_PIN_A17) |
           (1u << V30_PIN_A18) |
           (1u << V30_PIN_A19);
}

int main(void) {
    prepare_header_high_z();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 free-running raw bus timeline - 0.600 MHz\n");
    printf("Clock            : continuous PIO free-run\n");
    printf("RESET            : 20 counted clocks, release after falling edge\n");
    printf("Host AD drive    : disabled for entire capture\n");
    printf("Capture          : raw GPIO transition log, no bus service\n");
    printf("A/B reference    : compare against 0.300 MHz timeline\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    timeline_entry_t trace[TIMELINE_DEPTH] = {0};
    uint captured = 0u;
    bool reset_clock_ok = false;

    const uint32_t irq_state = save_and_disable_interrupts();

    release_ad();
    set_intr(false);
    hold_reset(true);
    perf_clock_start(&clock);

    reset_clock_ok = wait_reset_clocks(RESET_CLOCKS);
    if (reset_clock_ok) {
        hold_reset(false);

        const uint32_t mask = timeline_mask();
        const uint64_t start_us = time_us_64();
        uint32_t previous = sio_hw->gpio_in;

        trace[captured++] = (timeline_entry_t){
            .sequence = 0u,
            .sample = previous,
            .elapsed_us = 0u,
        };

        while (captured < TIMELINE_DEPTH) {
            const uint32_t sample = sio_hw->gpio_in;
            const uint64_t now_us = time_us_64();
            if ((sample & mask) != (previous & mask)) {
                trace[captured] = (timeline_entry_t){
                    .sequence = captured,
                    .sample = sample,
                    .elapsed_us = (uint32_t)(now_us - start_us),
                };
                ++captured;
                previous = sample;
            }
            if ((now_us - start_us) >= TIMELINE_MAX_US) break;
        }
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    restore_interrupts(irq_state);

    printf("RESET clock count = %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("Captured %u transition records\n\n", captured);
    printf(" seq  us  C A I D N B  A19:16  AD16   raw\n");
    printf("      |   L L O T T H\n");
    printf("      |   K E M R A E\n");

    for (uint i = 0u; i < captured; ++i) {
        const uint32_t s = trace[i].sample;
        printf("%4lu %3lu  %u %u %u %u %u %u    %X     %04X  %08lX\n",
               (unsigned long)trace[i].sequence,
               (unsigned long)trace[i].elapsed_us,
               (unsigned)sample_bit(s, V30_PIN_CLK),
               (unsigned)sample_bit(s, V30_PIN_ALE),
               (unsigned)sample_bit(s, V30_PIN_IOM),
               (unsigned)sample_bit(s, V30_PIN_DTR),
               (unsigned)sample_bit(s, V30_PIN_INTA),
               (unsigned)sample_bit(s, V30_PIN_BHE),
               (unsigned)decode_high_nibble(s),
               (unsigned)decode_ad(s),
               (unsigned long)s);
    }

    printf("\nPrimary A/B checks:\n");
    printf("- Reset-vector bus sequence should remain FFFF0 -> FFFF2 -> FFFF4 -> FFFF6.\n");
    printf("- Four-clock bus-cycle spacing should be about 6.67 us at 0.600 MHz.\n");
    printf("- CLK/ALE/control phase relationship should match the 0.300 MHz trace.\n");
    printf("- AD is never driven by RP2350 in this test.\n");
    printf("Canonical pc1a_rev1_300khz and the 0.300 MHz timeline remain unchanged.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
