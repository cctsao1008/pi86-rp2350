/*
 * Issue #106 Phase C1b: physical T1 pad capture + PIO address repack.
 *
 * One reset epoch is launched with a free-running processor clock. The PIO
 * state machine passively captures the first 16 physical ASTB/T1 snapshots,
 * emits each raw GPIO bank image, then repacks the canonical scattered
 * AD0..AD15/A16..A19 pins into linear A19..A0 using the Phase C1a primitive.
 *
 * M33 configures the finite experiment and verifies it afterwards. It does not
 * participate in any active bus cycle.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/processor_bus_pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"
#include "issue106_physical_t1_repack.pio.h"
#include "free_running_clock.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#define ISSUE106_VECTOR_COUNT 16u
#define ISSUE106_ADDRESS_BITS 20u
#define ISSUE106_MICRO_WORDS_PER_VECTOR (ISSUE106_ADDRESS_BITS + 1u)
#define ISSUE106_MICRO_WORD_COUNT \
    (ISSUE106_VECTOR_COUNT * ISSUE106_MICRO_WORDS_PER_VECTOR)
#define ISSUE106_CAPTURE_WORD_COUNT (ISSUE106_VECTOR_COUNT * 2u)
#define ISSUE106_PROCESSOR_HZ 1000000u
#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_REPACK_FINISH_REL 14u

static uint32_t micro_words[ISSUE106_MICRO_WORD_COUNT];
static uint32_t capture_words[ISSUE106_CAPTURE_WORD_COUNT];

static const uint8_t address_gpio[ISSUE106_ADDRESS_BITS] = {
    RP86_PROCESSOR_PIN_AD0,
    RP86_PROCESSOR_PIN_AD1,
    RP86_PROCESSOR_PIN_AD2,
    RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4,
    RP86_PROCESSOR_PIN_AD5,
    RP86_PROCESSOR_PIN_AD6,
    RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8,
    RP86_PROCESSOR_PIN_AD9,
    RP86_PROCESSOR_PIN_AD10,
    RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12,
    RP86_PROCESSOR_PIN_AD13,
    RP86_PROCESSOR_PIN_AD14,
    RP86_PROCESSOR_PIN_AD15,
    RP86_PROCESSOR_PIN_A16,
    RP86_PROCESSOR_PIN_A17,
    RP86_PROCESSOR_PIN_A18,
    RP86_PROCESSOR_PIN_A19,
};

static inline uint32_t sample_bit(uint32_t raw, uint gpio) {
    return (raw >> gpio) & 1u;
}

static uint32_t decode_raw_address(uint32_t raw) {
    uint32_t address = 0u;
    for (uint bit = 0u; bit < ISSUE106_ADDRESS_BITS; ++bit)
        address |= sample_bit(raw, address_gpio[bit]) << bit;
    return address & 0xfffffu;
}

static uint16_t extraction_instruction(uint gpio) {
    return gpio == 0u ? pio_encode_nop() : pio_encode_out(pio_null, gpio);
}

static void build_micro_stream(uint program_offset) {
    const uint16_t finish_jump =
        pio_encode_jmp(program_offset + ISSUE106_REPACK_FINISH_REL);
    uint cursor = 0u;

    for (uint vector = 0u; vector < ISSUE106_VECTOR_COUNT; ++vector) {
        for (int bit = ISSUE106_ADDRESS_BITS - 1; bit >= 0; --bit)
            micro_words[cursor++] =
                extraction_instruction(address_gpio[(uint)bit]);
        micro_words[cursor++] = finish_jump;
    }
    hard_assert(cursor == ISSUE106_MICRO_WORD_COUNT);
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

static void init_capture_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        issue106_physical_t1_repack_program_get_default_config(offset);

    sm_config_set_in_pins(&config, 0u);
    sm_config_set_out_shift(&config, true, false, 32u);
    sm_config_set_in_shift(&config, false, false, 32u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);

    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_clock_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        rp86_free_running_clock_program_get_default_config(offset);
    const float divider =
        (float)clock_get_hz(clk_sys) / (2.0f * (float)ISSUE106_PROCESSOR_HZ);

    sm_config_set_set_pins(&config, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(&config, divider);

    pio_gpio_init(pio, RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_set_enabled(pio, sm, true);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    bool last = gpio_get(RP86_PROCESSOR_PIN_CLK);
    uint falling_edges = 0u;

    while (falling_edges < count && time_us_64() <= deadline) {
        const bool now = gpio_get(RP86_PROCESSOR_PIN_CLK);
        if (last && !now) ++falling_edges;
        last = now;
        tight_loop_contents();
    }
    return falling_edges == count;
}

static bool wait_dma_idle(uint channel, uint64_t deadline) {
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

    printf("issue106 physical-T1 scattered-GPIO repack lab\n");
    printf("scope: physical ASTB/T1 pad snapshot -> PIO repack -> A19..A0\n");
    printf("processor clock: %u Hz; fixed-READY timing acceptance is NOT claimed\n",
           ISSUE106_PROCESSOR_HZ);

    prepare_header_safe();

    PIO capture_pio = pio0;
    const uint capture_sm = pio_claim_unused_sm(capture_pio, true);
    const uint capture_offset =
        pio_add_program(capture_pio, &issue106_physical_t1_repack_program);
    init_capture_sm(capture_pio, capture_sm, capture_offset);
    build_micro_stream(capture_offset);

    const uint tx_dma = dma_claim_unused_channel(true);
    dma_channel_config tx_cfg = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&tx_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_cfg, true);
    channel_config_set_write_increment(&tx_cfg, false);
    channel_config_set_dreq(
        &tx_cfg, pio_get_dreq(capture_pio, capture_sm, true));
    channel_config_set_high_priority(&tx_cfg, true);
    dma_channel_configure(tx_dma, &tx_cfg,
                          &capture_pio->txf[capture_sm],
                          micro_words, ISSUE106_MICRO_WORD_COUNT, false);

    const uint rx_dma = dma_claim_unused_channel(true);
    dma_channel_config rx_cfg = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&rx_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&rx_cfg, false);
    channel_config_set_write_increment(&rx_cfg, true);
    channel_config_set_dreq(
        &rx_cfg, pio_get_dreq(capture_pio, capture_sm, false));
    channel_config_set_high_priority(&rx_cfg, true);
    dma_channel_configure(rx_dma, &rx_cfg,
                          capture_words, &capture_pio->rxf[capture_sm],
                          ISSUE106_CAPTURE_WORD_COUNT, false);

    dma_start_channel_mask((1u << rx_dma) | (1u << tx_dma));

    PIO clock_pio = pio1;
    const uint clock_sm = pio_claim_unused_sm(clock_pio, true);
    const uint clock_offset =
        pio_add_program(clock_pio, &rp86_free_running_clock_program);
    init_clock_sm(clock_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool rx_complete = wait_dma_idle(rx_dma, deadline);

    /* Return the processor to a bounded safe state after the capture epoch. */
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    pio_sm_set_enabled(clock_pio, clock_sm, false);
    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint32_t rx_remaining =
        dma_channel_hw_addr(rx_dma)->transfer_count & 0x0fffffffu;
    const uint32_t words_captured =
        ISSUE106_CAPTURE_WORD_COUNT - rx_remaining;
    const uint32_t cycles_captured = words_captured / 2u;

    bool first_reset_vector_ok = false;
    uint failures = 0u;

    for (uint i = 0u; i < cycles_captured; ++i) {
        const uint32_t raw = capture_words[i * 2u];
        const uint32_t expected = decode_raw_address(raw);
        const uint32_t observed = capture_words[i * 2u + 1u] & 0xfffffu;
        const bool item_pass = expected == observed;

        if (i == 0u) first_reset_vector_ok = expected == 0xffff0u;

        printf("[%02u] raw=%08" PRIx32
               " expected=%05" PRIx32
               " observed=%05" PRIx32 " %s\n",
               i, raw, expected, observed, item_pass ? "PASS" : "FAIL");
        if (!item_pass) ++failures;
    }

    const bool complete = cycles_captured == ISSUE106_VECTOR_COUNT;
    const bool pass = reset_clock_ok && rx_complete && complete &&
                      first_reset_vector_ok && failures == 0u;

    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n",
           first_reset_vector_ok ? "PASS" : "FAIL");
    printf("CAPTURE: %u/%u cycles\n",
           cycles_captured, ISSUE106_VECTOR_COUNT);
    printf("RESULT: %s (%u/%u address mismatches)\n",
           pass ? "PASS" : "FAIL", failures, cycles_captured);
    printf("NOTE: physical T1 capture/repack only; lane handling, SRAM response ");
    printf("timing, and Intel 2 MHz no-wait acceptance are not proven.\n");

    while (true) tight_loop_contents();
}
