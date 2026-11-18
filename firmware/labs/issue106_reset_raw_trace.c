/*
 * Issue #106: synchronous Intel clock/reset + settled post-reset T1 observer.
 *
 * One PIO SM owns Intel-compliant CLK/RESET sequencing and raises IRQ0 at the
 * exact RESET-deassert boundary. A second SM waits on IRQ0 and captures settled
 * ASTB/T1 snapshots after a short fixed delay. No M33 action participates in
 * the active reset-to-first-cycle boundary.
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

#define TRACE_WORDS 16u
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

static void init_clock_reset(PIO pio, uint sm, uint offset) {
    pio_sm_config cfg =
        issue106_intel_clock_reset_program_get_default_config(offset);

    const float div =
        (float)clock_get_hz(clk_sys) /
        (5.0f * (float)ISSUE106_PROCESSOR_HZ);

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
    pio_sm_set_enabled(pio, sm, false);
}

static void init_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config cfg =
        issue106_post_reset_t1_observer_program_get_default_config(offset);

    sm_config_set_in_pins(&cfg, 0u);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);

    hard_assert(pio_sm_init(pio, sm, offset, &cfg) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
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

    printf("issue106 synchronous Intel reset + AF address observer\n");
    printf("processor clock: %u Hz (2-high/3-low PIO waveform)\n",
           ISSUE106_PROCESSOR_HZ);

    prepare_header_safe();

    PIO pio = pio0;
    const uint clock_sm = pio_claim_unused_sm(pio, true);
    const uint observer_sm = pio_claim_unused_sm(pio, true);

    const uint clock_offset =
        pio_add_program(pio, &issue106_intel_clock_reset_program);
    const uint observer_offset =
        pio_add_program(pio, &issue106_post_reset_t1_observer_program);

    init_clock_reset(pio, clock_sm, clock_offset);
    init_observer(pio, observer_sm, observer_offset);

    const uint rx_dma = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio, observer_sm, false));
    channel_config_set_high_priority(&cfg, true);

    dma_channel_configure(
        rx_dma, &cfg, trace_words, &pio->rxf[observer_sm],
        TRACE_WORDS, true);

    /*
     * Observer is already blocked on IRQ0 before the clock/reset SM starts.
     * The clock/reset SM emits eight compliant reset clocks, deasserts RESET,
     * raises IRQ0, and continues free-running. No M33 boundary action exists.
     */
    pio_interrupt_clear(pio, 0u);
    pio_sm_set_enabled(pio, observer_sm, true);
    pio_sm_set_enabled(pio, clock_sm, true);

    const bool trace_ok = wait_dma(rx_dma);

    pio_sm_set_enabled(pio, clock_sm, false);
    pio_sm_set_enabled(pio, observer_sm, false);

    gpio_set_function(RP86_PROCESSOR_PIN_RESET, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);

    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint32_t remaining =
        dma_channel_hw_addr(rx_dma)->transfer_count & 0x0fffffffu;
    const uint32_t captured = TRACE_WORDS - remaining;

    printf("RESET SEQUENCE: HARDWARE-SYNCHRONOUS 8 CLOCKS\n");
    printf("TRACE: %s (%" PRIu32 "/%u AF samples)\n",
           trace_ok ? "COMPLETE" : "TIMEOUT", captured, TRACE_WORDS);

    for (uint32_t i = 0u; i < captured; ++i) {
        const uint32_t raw = trace_words[i];
        printf("[%02" PRIu32 "] addr=%05" PRIx32
               " RESET=%u ASTB=%u IOM=%u BUFRW=%u INTAK=%u raw=%08" PRIx32
               "%s\n",
               i, decode_address(raw),
               sample_bit(raw, RP86_PROCESSOR_PIN_RESET),
               sample_bit(raw, RP86_PROCESSOR_PIN_ASTB),
               sample_bit(raw, RP86_PROCESSOR_PIN_IOM),
               sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW),
               sample_bit(raw, RP86_PROCESSOR_PIN_INTAK),
               raw,
               i == 0u ? "  <FIRST-POST-RESET-AF>" : "");
    }

    if (captured > 0u) {
        printf("FIRST POST-RESET AF ADDRESS: %05" PRIx32 "\n",
               decode_address(trace_words[0]));
        printf("FIRST RESET VECTOR FFFF0: %s\n",
               decode_address(trace_words[0]) == 0xffff0u ? "PASS" : "FAIL");
    } else {
        printf("FIRST POST-RESET AF ADDRESS: NONE\n");
        printf("FIRST RESET VECTOR FFFF0: FAIL\n");
    }

    printf("NOTE: RESET release is PIO-synchronous; address is sampled at ASTB falling edge.\n");

    while (true) tight_loop_contents();
}
