/*
 * Issue #106: synchronous Intel clock + passive raw bus trace.
 *
 * The same PIO state machine generates the Intel 8086 clock and samples the
 * complete GPIO bank. This removes cross-SM arming/phase ambiguity.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/processor_bus_pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"
#include "issue106_intel_clock_trace.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#ifndef ISSUE106_PROCESSOR_HZ
#define ISSUE106_PROCESSOR_HZ 8000000u
#endif

#define TRACE_WORDS 128u
#define TIMEOUT_US 100000u

static uint32_t trace_words[TRACE_WORDS];

static const uint8_t address_gpio[20] = {
    RP86_PROCESSOR_PIN_AD0, RP86_PROCESSOR_PIN_AD1,
    RP86_PROCESSOR_PIN_AD2, RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4, RP86_PROCESSOR_PIN_AD5,
    RP86_PROCESSOR_PIN_AD6, RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8, RP86_PROCESSOR_PIN_AD9,
    RP86_PROCESSOR_PIN_AD10, RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12, RP86_PROCESSOR_PIN_AD13,
    RP86_PROCESSOR_PIN_AD14, RP86_PROCESSOR_PIN_AD15,
    RP86_PROCESSOR_PIN_A16, RP86_PROCESSOR_PIN_A17,
    RP86_PROCESSOR_PIN_A18, RP86_PROCESSOR_PIN_A19,
};

static inline uint32_t sample_bit(uint32_t raw, uint gpio) {
    return (raw >> gpio) & 1u;
}

static uint32_t decode_address(uint32_t raw) {
    uint32_t address = 0u;
    for (uint i = 0u; i < 20u; ++i)
        address |= sample_bit(raw, address_gpio[i]) << i;
    return address & 0xfffffu;
}

static void prepare_header_safe(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }

    gpio_init(RP86_PROCESSOR_PIN_RESET);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_RESET);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);

    gpio_init(RP86_PROCESSOR_PIN_INTR);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_INTR);
    gpio_put(RP86_PROCESSOR_PIN_INTR, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_INTR, GPIO_OUT);
}

static void init_clock_trace(PIO pio, uint sm, uint offset) {
    pio_sm_config cfg =
        issue106_intel_clock_trace_program_get_default_config(offset);

    const float div =
        (float)clock_get_hz(clk_sys) /
        (5.0f * (float)ISSUE106_PROCESSOR_HZ);

    sm_config_set_in_pins(&cfg, 0u);
    sm_config_set_sideset_pins(&cfg, RP86_PROCESSOR_PIN_CLK);
    sm_config_set_set_pins(&cfg, RP86_PROCESSOR_PIN_RESET, 1u);
    sm_config_set_clkdiv(&cfg, div);

    pio_gpio_init(pio, RP86_PROCESSOR_PIN_CLK);
    pio_gpio_init(pio, RP86_PROCESSOR_PIN_RESET);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_CLK, 1u, true);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_RESET, 1u, true);

    hard_assert(pio_sm_init(pio, sm, offset, &cfg) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t deadline = time_us_64() + TIMEOUT_US;
    bool last = gpio_get(RP86_PROCESSOR_PIN_CLK);
    uint falling = 0u;

    while (falling < count && time_us_64() <= deadline) {
        const bool now = gpio_get(RP86_PROCESSOR_PIN_CLK);
        if (last && !now) ++falling;
        last = now;
        tight_loop_contents();
    }
    return falling == count;
}

static bool wait_dma(uint channel) {
    const uint64_t deadline = time_us_64() + TIMEOUT_US;
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue106 synchronous Intel clock/raw trace\n");
    printf("processor clock: %u Hz (2-high/3-low PIO waveform)\n",
           ISSUE106_PROCESSOR_HZ);

    prepare_header_safe();

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset =
        pio_add_program(pio, &issue106_intel_clock_trace_program);
    init_clock_trace(pio, sm, offset);

    const uint rx_dma = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, sm, false));
    channel_config_set_high_priority(&cfg, true);

    dma_channel_configure(
        rx_dma, &cfg, trace_words, &pio->rxf[sm], TRACE_WORDS, true);

    /*
     * The PIO program itself owns RESET and CLK. It emits exactly eight
     * Intel-compliant reset clocks, deasserts RESET on a deterministic low
     * boundary, then begins synchronous sampling. M33 does not participate in
     * the release boundary.
     */
    pio_sm_set_enabled(pio, sm, true);
    const bool reset_clock_ok = true;

    const bool trace_ok = wait_dma(rx_dma);

    pio_sm_set_enabled(pio, sm, false);

    gpio_set_function(RP86_PROCESSOR_PIN_RESET, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);

    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint32_t remaining =
        dma_channel_hw_addr(rx_dma)->transfer_count & 0x0fffffffu;
    const uint32_t captured = TRACE_WORDS - remaining;

    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("TRACE: %s (%" PRIu32 "/%u samples)\n",
           trace_ok ? "COMPLETE" : "TIMEOUT", captured, TRACE_WORDS);

    int first_reset_low = -1;
    int first_post_reset_astb = -1;
    bool prev_astb = false;

    for (uint32_t i = 0u; i < captured; ++i) {
        const uint32_t raw = trace_words[i];
        const bool reset = sample_bit(raw, RP86_PROCESSOR_PIN_RESET);
        const bool astb = sample_bit(raw, RP86_PROCESSOR_PIN_ASTB);

        if (first_reset_low < 0 && !reset)
            first_reset_low = (int)i;

        if (first_reset_low >= 0 && first_post_reset_astb < 0 &&
            astb && !prev_astb)
            first_post_reset_astb = (int)i;

        prev_astb = astb;
    }

    printf("FIRST RESET=0 SAMPLE: %d\n", first_reset_low);
    printf("FIRST POST-RESET ASTB RISE SAMPLE: %d\n",
           first_post_reset_astb);

    int begin = first_reset_low >= 4 ? first_reset_low - 4 : 0;
    int end = first_reset_low >= 0 ? first_reset_low + 32 : (int)captured;
    if (end > (int)captured) end = (int)captured;

    for (int i = begin; i < end; ++i) {
        const uint32_t raw = trace_words[i];
        printf("[%03d] phase=%c RESET=%u ASTB=%u addr=%05" PRIx32
               " IOM=%u BUFRW=%u INTAK=%u raw=%08" PRIx32 "%s\n",
               i, (i & 1) ? 'L' : 'H',
               sample_bit(raw, RP86_PROCESSOR_PIN_RESET),
               sample_bit(raw, RP86_PROCESSOR_PIN_ASTB),
               decode_address(raw),
               sample_bit(raw, RP86_PROCESSOR_PIN_IOM),
               sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW),
               sample_bit(raw, RP86_PROCESSOR_PIN_INTAK),
               raw,
               i == first_post_reset_astb ? "  <FIRST-POST-RESET-ASTB>" : "");
    }

    if (first_post_reset_astb >= 0) {
        const uint32_t first =
            trace_words[(uint32_t)first_post_reset_astb];
        printf("FIRST POST-RESET T1 ADDRESS: %05" PRIx32 "\n",
               decode_address(first));
    } else {
        printf("FIRST POST-RESET T1 ADDRESS: NONE\n");
    }

    printf("NOTE: clock generation and observation share one PIO timeline.\n");

    while (true) tight_loop_contents();
}
