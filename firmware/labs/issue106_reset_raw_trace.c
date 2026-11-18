/*
 * Issue #106: passive raw reset/bus trace for high-frequency diagnostics.
 *
 * Samples GPIO0..31 on every processor CLK edge after RESET deassertion.
 * No realtime repack and no bus response are performed. M33 decodes the
 * captured samples only after the finite trace is complete.
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
#include "issue106_reset_raw_trace.pio.h"
#include "free_running_clock.pio.h"
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

static inline uint32_t bit(uint32_t raw, uint gpio) {
    return (raw >> gpio) & 1u;
}

static uint32_t decode_address(uint32_t raw) {
    uint32_t address = 0u;
    for (uint i = 0; i < 20u; ++i)
        address |= bit(raw, address_gpio[i]) << i;
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

static void init_trace(PIO pio, uint sm, uint offset) {
    pio_sm_config cfg =
        issue106_reset_raw_trace_program_get_default_config(offset);
    sm_config_set_in_pins(&cfg, 0u);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(pio, sm, offset, &cfg) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    /* Armed explicitly at the RESET-release boundary in main(). */
    pio_sm_set_enabled(pio, sm, false);
}

static void init_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config cfg =
        rp86_free_running_clock_program_get_default_config(offset);
    const float div =
        (float)clock_get_hz(clk_sys) / (2.0f * (float)ISSUE106_PROCESSOR_HZ);

    sm_config_set_set_pins(&cfg, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(&cfg, div);
    pio_gpio_init(pio, RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(pio, sm, offset, &cfg) == PICO_OK);
    pio_sm_set_enabled(pio, sm, true);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t deadline = time_us_64() + TIMEOUT_US;
    bool last = gpio_get(RP86_PROCESSOR_PIN_CLK);
    uint falling = 0u;
    while (falling < count && time_us_64() <= deadline) {
        bool now = gpio_get(RP86_PROCESSOR_PIN_CLK);
        if (last && !now) ++falling;
        last = now;
        tight_loop_contents();
    }
    return falling == count;
}

static bool wait_dma(uint ch) {
    const uint64_t deadline = time_us_64() + TIMEOUT_US;
    while (dma_channel_is_busy(ch)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue106 passive reset raw trace\n");
    printf("processor clock: %u Hz\n", ISSUE106_PROCESSOR_HZ);

    prepare_header_safe();

    PIO trace_pio = pio0;
    uint trace_sm = pio_claim_unused_sm(trace_pio, true);
    uint trace_off =
        pio_add_program(trace_pio, &issue106_reset_raw_trace_program);
    init_trace(trace_pio, trace_sm, trace_off);

    uint rx_dma = dma_claim_unused_channel(true);
    dma_channel_config rx_cfg = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&rx_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_cfg, false);
    channel_config_set_write_increment(&rx_cfg, true);
    channel_config_set_dreq(
        &rx_cfg, pio_get_dreq(trace_pio, trace_sm, false));
    channel_config_set_high_priority(&rx_cfg, true);
    dma_channel_configure(rx_dma, &rx_cfg,
                          trace_words, &trace_pio->rxf[trace_sm],
                          TRACE_WORDS, true);

    PIO clock_pio = pio1;
    uint clock_sm = pio_claim_unused_sm(clock_pio, true);
    uint clock_off =
        pio_add_program(clock_pio, &rp86_free_running_clock_program);
    init_clock(clock_pio, clock_sm, clock_off);

    bool reset_clock_ok = wait_reset_clocks(8u);

    /*
     * Arm the passive trace at the final RESET-release boundary. Keeping the
     * SM disabled during reset qualification prevents RESET-time ASTB activity
     * from consuming capture slots. DMA is already armed and waiting on RX
     * DREQ before this point.
     */
    pio_sm_restart(trace_pio, trace_sm);
    pio_sm_exec(trace_pio, trace_sm, pio_encode_jmp(trace_off));
    pio_sm_set_enabled(trace_pio, trace_sm, true);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    bool trace_ok = wait_dma(rx_dma);

    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    pio_sm_set_enabled(clock_pio, clock_sm, false);

    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    uint32_t remaining =
        dma_channel_hw_addr(rx_dma)->transfer_count & 0x0fffffffu;
    uint32_t captured = TRACE_WORDS - remaining;

    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("TRACE: %s (%" PRIu32 "/%u samples)\n",
           trace_ok ? "COMPLETE" : "TIMEOUT", captured, TRACE_WORDS);

    for (uint i = 0u; i < captured; ++i) {
        uint32_t raw = trace_words[i];
        printf("[%02u] addr=%05" PRIx32
               " IOM=%u BUFRW=%u INTAK=%u raw=%08" PRIx32 "\n",
               i, decode_address(raw),
               bit(raw, RP86_PROCESSOR_PIN_IOM),
               bit(raw, RP86_PROCESSOR_PIN_BUFRW),
               bit(raw, RP86_PROCESSOR_PIN_INTAK),
               raw);
    }


    printf("NOTE: passive settled-T1 ASTB trace only; no responder is active.\n");

    while (true) tight_loop_contents();
}
