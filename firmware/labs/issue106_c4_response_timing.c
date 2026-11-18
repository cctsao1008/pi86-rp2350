/*
 * Issue #106 Phase C4: one-shot end-to-end physical read response timing lab.
 *
 * Services only the architectural reset-vector WORD read at FFFF0h.
 * The SRAM word is EB FE (JMP $), little-endian FEEBh.
 *
 * Active dataplane:
 *   T1 capture
 *   -> 64 KiB-aligned backing pointer
 *   -> DMA raw SRAM read
 *   -> PIO low/high byte LUT pointer construction
 *   -> two 256-entry internal-SRAM LUT lookups
 *   -> DMA sniffer SUM (equivalent to OR because LUT GPIO bits are disjoint)
 *   -> responder PIO scattered AD drive/release
 *
 * M33 configures and verifies only; it does not participate in the active
 * transaction.
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
#include "hardware/structs/sio.h"
#include "issue106_c4_response_timing.pio.h"
#include "free_running_clock.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#define ISSUE106_PROCESSOR_HZ 1000000u
#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_FRONTEND_BITS 16u
#define ISSUE106_FRONTEND_WORDS (1u + ISSUE106_FRONTEND_BITS + 1u)
#define ISSUE106_FRONTEND_FINISH_REL 16u
#define ISSUE106_OBSERVER_WORDS 5u
#define ISSUE106_RESET_WORD 0xfeeBu
#define ISSUE106_DMA_SNIFF_SUM_MODE 0x0fu

static uint8_t backing[65536] __attribute__((aligned(65536)));
static uint32_t low_lut[256] __attribute__((aligned(1024)));
static uint32_t high_lut[256] __attribute__((aligned(1024)));
static uint32_t frontend_words[ISSUE106_FRONTEND_WORDS];
static uint32_t observer_words[ISSUE106_OBSERVER_WORDS];
static uint32_t encoded_parts[2];
static uint32_t sniff_sink;
static uint32_t high_lut_prefix;

static const uint8_t ad_gpio[16] = {
    RP86_PROCESSOR_PIN_AD0, RP86_PROCESSOR_PIN_AD1,
    RP86_PROCESSOR_PIN_AD2, RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4, RP86_PROCESSOR_PIN_AD5,
    RP86_PROCESSOR_PIN_AD6, RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8, RP86_PROCESSOR_PIN_AD9,
    RP86_PROCESSOR_PIN_AD10, RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12, RP86_PROCESSOR_PIN_AD13,
    RP86_PROCESSOR_PIN_AD14, RP86_PROCESSOR_PIN_AD15,
};

static inline uint32_t sample_bit(uint32_t raw, uint gpio) {
    return (raw >> gpio) & 1u;
}

static uint16_t decode_ad(uint32_t raw) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(raw, ad_gpio[bit]) << bit);
    return value;
}

static uint32_t decode_address(uint32_t raw) {
    uint32_t address = decode_ad(raw);
    address |= sample_bit(raw, RP86_PROCESSOR_PIN_A16) << 16;
    address |= sample_bit(raw, RP86_PROCESSOR_PIN_A17) << 17;
    address |= sample_bit(raw, RP86_PROCESSOR_PIN_A18) << 18;
    address |= sample_bit(raw, RP86_PROCESSOR_PIN_A19) << 19;
    return address & 0xfffffu;
}

static bool is_word_lane(uint32_t raw) {
    return sample_bit(raw, RP86_PROCESSOR_PIN_AD0) == 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_UBE) == 0u;
}

static bool is_memory_read(uint32_t raw) {
    return sample_bit(raw, RP86_PROCESSOR_PIN_IOM) != 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) == 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_INTAK) != 0u;
}

static uint32_t encode_low_byte(uint8_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 8u; ++bit)
        if ((value >> bit) & 1u) encoded |= 1u << ad_gpio[bit];
    return encoded;
}

static uint32_t encode_high_byte(uint8_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 8u; ++bit)
        if ((value >> bit) & 1u) encoded |= 1u << ad_gpio[bit + 8u];
    return encoded;
}

static uint16_t extraction_instruction(uint gpio) {
    return gpio == 0u ? pio_encode_nop() : pio_encode_out(pio_null, gpio);
}

static void build_frontend_stream(uint program_offset) {
    const uint32_t backing_hi =
        ((uint32_t)(uintptr_t)backing >> 16) & 0xffffu;
    const uint16_t finish_jump =
        pio_encode_jmp(program_offset + ISSUE106_FRONTEND_FINISH_REL);

    uint cursor = 0u;
    frontend_words[cursor++] = backing_hi;
    for (int bit = ISSUE106_FRONTEND_BITS - 1; bit >= 0; --bit)
        frontend_words[cursor++] =
            extraction_instruction(ad_gpio[(uint)bit]);
    frontend_words[cursor++] = finish_jump;
    hard_assert(cursor == ISSUE106_FRONTEND_WORDS);
}

static void prepare_memory(void) {
    for (uint i = 0u; i < sizeof backing; ++i) backing[i] = 0x90u;
    backing[0xfff0u] = 0xebu;
    backing[0xfff1u] = 0xfeu;

    for (uint i = 0u; i < 256u; ++i) {
        low_lut[i] = encode_low_byte((uint8_t)i);
        high_lut[i] = encode_high_byte((uint8_t)i);
    }
    encoded_parts[0] = 0u;
    encoded_parts[1] = 0u;
    sniff_sink = 0u;
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

static void init_frontend(PIO pio, uint sm, uint offset) {
    pio_sm_config c = issue106_c4_frontend_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_in_shift(&c, false, false, 32u);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void init_byte_lut_builder(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c4_byte_lut_pointers_program_get_default_config(offset);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_in_shift(&c, false, false, 32u);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);

    const uint32_t low_prefix =
        ((uint32_t)(uintptr_t)low_lut >> 10) & 0x003fffffu;
    high_lut_prefix =
        ((uint32_t)(uintptr_t)high_lut >> 10) & 0x003fffffu;
    pio_sm_put_blocking(pio, sm, low_prefix);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_responder(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c4_responder_program_get_default_config(offset);
    sm_config_set_out_pins(&c, 0u, 28u);
    sm_config_set_out_shift(&c, true, false, 32u);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);

    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(pio, ad_gpio[bit]);

    pio_sm_set_consecutive_pindirs(pio, sm, 0u, 28u, false);
    pio_sm_clear_fifos(pio, sm);
    /*
     * PIO2 also owns the free-running processor clock on GPIO21. Preserve
     * that direction bit whenever the responder changes PINDIRS; otherwise
     * enabling AD would accidentally tri-state CLK and truncate the cycle.
     */
    pio_sm_put_blocking(
        pio, sm,
        RP86_PROCESSOR_AD_BUS_MASK | (1u << RP86_PROCESSOR_PIN_CLK));
    pio_sm_set_enabled(pio, sm, true);
}

static void init_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c4_observer_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void init_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        rp86_free_running_clock_program_get_default_config(offset);
    const float divider =
        (float)clock_get_hz(clk_sys) / (2.0f * (float)ISSUE106_PROCESSOR_HZ);
    sm_config_set_set_pins(&c, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c, divider);
    pio_gpio_init(pio, RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_set_enabled(pio, sm, true);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
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

static bool wait_dma_idle(uint channel, uint64_t deadline) {
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

static const char *phase_name(uint index) {
    static const char *const names[ISSUE106_OBSERVER_WORDS] = {
        "T1", "AF", "R1", "F1", "R2"
    };
    return index < ISSUE106_OBSERVER_WORDS ? names[index] : "?";
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue106 C4 end-to-end read response timing lab\n");
    printf("scope: T1 -> raw SRAM -> byte LUTs -> DMA SUM -> scattered AD drive\n");
    printf("processor clock: %u Hz (empirical; Intel compliance not claimed)\n",
           ISSUE106_PROCESSOR_HZ);

    const uintptr_t backing_addr = (uintptr_t)backing;
    const uintptr_t low_lut_addr = (uintptr_t)low_lut;
    const uintptr_t high_lut_addr = (uintptr_t)high_lut;
    hard_assert((backing_addr & 0xffffu) == 0u);
    hard_assert((low_lut_addr & 0x3ffu) == 0u);
    hard_assert((high_lut_addr & 0x3ffu) == 0u);

    prepare_memory();
    prepare_header_safe();

    PIO frontend_pio = pio0;
    const uint frontend_sm = pio_claim_unused_sm(frontend_pio, true);
    const uint frontend_offset =
        pio_add_program(frontend_pio, &issue106_c4_frontend_program);
    init_frontend(frontend_pio, frontend_sm, frontend_offset);
    build_frontend_stream(frontend_offset);

    PIO builder_pio = pio1;
    const uint builder_sm = pio_claim_unused_sm(builder_pio, true);
    const uint builder_offset =
        pio_add_program(builder_pio, &issue106_c4_byte_lut_pointers_program);
    init_byte_lut_builder(builder_pio, builder_sm, builder_offset);

    PIO io_pio = pio2;
    const uint responder_sm = pio_claim_unused_sm(io_pio, true);
    const uint observer_sm = pio_claim_unused_sm(io_pio, true);
    const uint clock_sm = pio_claim_unused_sm(io_pio, true);
    const uint responder_offset =
        pio_add_program(io_pio, &issue106_c4_responder_program);
    const uint observer_offset =
        pio_add_program(io_pio, &issue106_c4_observer_program);
    const uint clock_offset =
        pio_add_program(io_pio, &rp86_free_running_clock_program);
    init_responder(io_pio, responder_sm, responder_offset);
    init_observer(io_pio, observer_sm, observer_offset);

    const uint frontend_tx_dma = dma_claim_unused_channel(true);
    dma_channel_config frontend_tx_cfg =
        dma_channel_get_default_config(frontend_tx_dma);
    channel_config_set_transfer_data_size(&frontend_tx_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&frontend_tx_cfg, true);
    channel_config_set_write_increment(&frontend_tx_cfg, false);
    channel_config_set_dreq(
        &frontend_tx_cfg, pio_get_dreq(frontend_pio, frontend_sm, true));
    channel_config_set_high_priority(&frontend_tx_cfg, true);
    dma_channel_configure(frontend_tx_dma, &frontend_tx_cfg,
                          &frontend_pio->txf[frontend_sm],
                          frontend_words, ISSUE106_FRONTEND_WORDS, false);

    const uint prefix_dma = dma_claim_unused_channel(true);
    dma_channel_config prefix_cfg = dma_channel_get_default_config(prefix_dma);
    channel_config_set_transfer_data_size(&prefix_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&prefix_cfg, false);
    channel_config_set_write_increment(&prefix_cfg, false);
    channel_config_set_dreq(
        &prefix_cfg, pio_get_dreq(builder_pio, builder_sm, true));
    channel_config_set_high_priority(&prefix_cfg, true);
    dma_channel_configure(prefix_dma, &prefix_cfg,
                          &builder_pio->txf[builder_sm],
                          &high_lut_prefix, 1u, false);

    const uint raw_data_dma = dma_claim_unused_channel(true);
    dma_channel_config raw_data_cfg =
        dma_channel_get_default_config(raw_data_dma);
    channel_config_set_transfer_data_size(&raw_data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&raw_data_cfg, false);
    channel_config_set_write_increment(&raw_data_cfg, false);
    channel_config_set_dreq(
        &raw_data_cfg, pio_get_dreq(builder_pio, builder_sm, true));
    channel_config_set_chain_to(&raw_data_cfg, prefix_dma);
    channel_config_set_high_priority(&raw_data_cfg, true);
    dma_channel_configure(raw_data_dma, &raw_data_cfg,
                          &builder_pio->txf[builder_sm], backing,
                          1u, false);

    const uint frontend_control_dma = dma_claim_unused_channel(true);
    dma_channel_config frontend_control_cfg =
        dma_channel_get_default_config(frontend_control_dma);
    channel_config_set_transfer_data_size(&frontend_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&frontend_control_cfg, false);
    channel_config_set_write_increment(&frontend_control_cfg, false);
    channel_config_set_dreq(
        &frontend_control_cfg,
        pio_get_dreq(frontend_pio, frontend_sm, false));
    channel_config_set_high_priority(&frontend_control_cfg, true);
    dma_channel_configure(
        frontend_control_dma, &frontend_control_cfg,
        &dma_channel_hw_addr(raw_data_dma)->al3_read_addr_trig,
        &frontend_pio->rxf[frontend_sm], 1u, false);

    const uint final_dma = dma_claim_unused_channel(true);
    dma_channel_config final_cfg = dma_channel_get_default_config(final_dma);
    channel_config_set_transfer_data_size(&final_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&final_cfg, false);
    channel_config_set_write_increment(&final_cfg, false);
    channel_config_set_dreq(
        &final_cfg, pio_get_dreq(io_pio, responder_sm, true));
    channel_config_set_high_priority(&final_cfg, true);
    dma_channel_configure(final_dma, &final_cfg,
                          &io_pio->txf[responder_sm],
                          &dma_hw->sniff_data, 1u, false);

    const uint combine_dma = dma_claim_unused_channel(true);
    dma_channel_config combine_cfg =
        dma_channel_get_default_config(combine_dma);
    channel_config_set_transfer_data_size(&combine_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&combine_cfg, true);
    channel_config_set_write_increment(&combine_cfg, false);
    channel_config_set_dreq(&combine_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&combine_cfg, final_dma);
    channel_config_set_sniff_enable(&combine_cfg, true);
    channel_config_set_high_priority(&combine_cfg, true);
    dma_channel_configure(combine_dma, &combine_cfg,
                          &sniff_sink, encoded_parts, 2u, false);

    const uint high_lookup_dma = dma_claim_unused_channel(true);
    dma_channel_config high_lookup_cfg =
        dma_channel_get_default_config(high_lookup_dma);
    channel_config_set_transfer_data_size(&high_lookup_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&high_lookup_cfg, false);
    channel_config_set_write_increment(&high_lookup_cfg, false);
    channel_config_set_dreq(&high_lookup_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&high_lookup_cfg, combine_dma);
    channel_config_set_high_priority(&high_lookup_cfg, true);
    dma_channel_configure(high_lookup_dma, &high_lookup_cfg,
                          &encoded_parts[1], high_lut, 1u, false);

    const uint high_control_dma = dma_claim_unused_channel(true);
    dma_channel_config high_control_cfg =
        dma_channel_get_default_config(high_control_dma);
    channel_config_set_transfer_data_size(&high_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&high_control_cfg, false);
    channel_config_set_write_increment(&high_control_cfg, false);
    channel_config_set_dreq(
        &high_control_cfg, pio_get_dreq(builder_pio, builder_sm, false));
    channel_config_set_high_priority(&high_control_cfg, true);
    dma_channel_configure(
        high_control_dma, &high_control_cfg,
        &dma_channel_hw_addr(high_lookup_dma)->al3_read_addr_trig,
        &builder_pio->rxf[builder_sm], 1u, false);

    const uint low_lookup_dma = dma_claim_unused_channel(true);
    dma_channel_config low_lookup_cfg =
        dma_channel_get_default_config(low_lookup_dma);
    channel_config_set_transfer_data_size(&low_lookup_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&low_lookup_cfg, false);
    channel_config_set_write_increment(&low_lookup_cfg, false);
    channel_config_set_dreq(&low_lookup_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&low_lookup_cfg, high_control_dma);
    channel_config_set_high_priority(&low_lookup_cfg, true);
    dma_channel_configure(low_lookup_dma, &low_lookup_cfg,
                          &encoded_parts[0], low_lut, 1u, false);

    const uint low_control_dma = dma_claim_unused_channel(true);
    dma_channel_config low_control_cfg =
        dma_channel_get_default_config(low_control_dma);
    channel_config_set_transfer_data_size(&low_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&low_control_cfg, false);
    channel_config_set_write_increment(&low_control_cfg, false);
    channel_config_set_dreq(
        &low_control_cfg, pio_get_dreq(builder_pio, builder_sm, false));
    channel_config_set_high_priority(&low_control_cfg, true);
    dma_channel_configure(
        low_control_dma, &low_control_cfg,
        &dma_channel_hw_addr(low_lookup_dma)->al3_read_addr_trig,
        &builder_pio->rxf[builder_sm], 1u, false);

    const uint observer_dma = dma_claim_unused_channel(true);
    dma_channel_config observer_cfg =
        dma_channel_get_default_config(observer_dma);
    channel_config_set_transfer_data_size(&observer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&observer_cfg, false);
    channel_config_set_write_increment(&observer_cfg, true);
    channel_config_set_dreq(
        &observer_cfg, pio_get_dreq(io_pio, observer_sm, false));
    channel_config_set_high_priority(&observer_cfg, true);
    dma_channel_configure(observer_dma, &observer_cfg,
                          observer_words, &io_pio->rxf[observer_sm],
                          ISSUE106_OBSERVER_WORDS, false);

    dma_sniffer_set_data_accumulator(0u);
    dma_sniffer_enable(
        combine_dma, ISSUE106_DMA_SNIFF_SUM_MODE, false);

    dma_start_channel_mask(
        (1u << frontend_tx_dma) |
        (1u << frontend_control_dma) |
        (1u << low_control_dma) |
        (1u << observer_dma));

    init_clock(io_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);

    /*
     * Arm capture only after reset qualification. This prevents either SM
     * from consuming a RESET-time ASTB transition or initialization glitch.
     * Both start from the same clean first-cycle boundary immediately before
     * RESET is released.
     */
    pio_sm_restart(frontend_pio, frontend_sm);
    pio_sm_exec(frontend_pio, frontend_sm,
                pio_encode_jmp(frontend_offset));
    pio_sm_restart(io_pio, observer_sm);
    pio_sm_exec(io_pio, observer_sm,
                pio_encode_jmp(observer_offset));
    pio_sm_set_enabled(frontend_pio, frontend_sm, true);
    pio_sm_set_enabled(io_pio, observer_sm, true);

    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool observer_complete = wait_dma_idle(observer_dma, deadline);
    const uint32_t observer_remaining =
        dma_channel_hw_addr(observer_dma)->transfer_count & 0x0fffffffu;

    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    pio_sm_set_enabled(io_pio, clock_sm, false);

    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_gpio[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = RP86_PROCESSOR_AD_BUS_MASK;

    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint32_t t1_raw = observer_words[0];
    const uint32_t address = decode_address(t1_raw);
    const bool address_ok = address == 0xffff0u;
    const bool lane_ok = is_word_lane(t1_raw);
    const bool read_ok = is_memory_read(t1_raw);

    int first_valid_phase = -1;
    uint phase_matches = 0u;

    printf("BACKING BASE:  %08" PRIxPTR "\n", backing_addr);
    printf("LOW LUT BASE:  %08" PRIxPTR "\n", low_lut_addr);
    printf("HIGH LUT BASE: %08" PRIxPTR "\n", high_lut_addr);
    printf("T1: raw=%08" PRIx32 " addr=%05" PRIx32
           " word=%s memread=%s\n",
           t1_raw, address,
           lane_ok ? "YES" : "NO",
           read_ok ? "YES" : "NO");

    for (uint i = 1u; i < ISSUE106_OBSERVER_WORDS; ++i) {
        const uint16_t data = decode_ad(observer_words[i]);
        const bool match = data == ISSUE106_RESET_WORD;
        printf("%s: raw=%08" PRIx32 " data=%04x %s\n",
               phase_name(i), observer_words[i], data,
               match ? "MATCH" : "----");
        if (match) {
            ++phase_matches;
            if (first_valid_phase < 0) first_valid_phase = (int)i;
        }
    }

    const bool raw_done = !dma_channel_is_busy(raw_data_dma);
    const bool low_done = !dma_channel_is_busy(low_lookup_dma);
    const bool high_done = !dma_channel_is_busy(high_lookup_dma);
    const bool combine_done = !dma_channel_is_busy(combine_dma);
    const bool final_done = !dma_channel_is_busy(final_dma);
    const uint32_t encoded_word = dma_sniffer_get_data_accumulator();

    printf("OBSERVER: %s (%u/%u words captured)\n",
           observer_complete ? "COMPLETE" : "TIMEOUT",
           ISSUE106_OBSERVER_WORDS - observer_remaining,
           ISSUE106_OBSERVER_WORDS);
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n", address_ok ? "PASS" : "FAIL");
    printf("FIRST CYCLE WORD MEMREAD: %s\n",
           lane_ok && read_ok ? "PASS" : "FAIL");
    printf("ENCODE PARTS: low=%08" PRIx32 " high=%08" PRIx32
           " sum=%08" PRIx32 "\n",
           encoded_parts[0], encoded_parts[1], encoded_word);
    printf("DMA PIPELINE: raw=%s low=%s high=%s sum=%s final=%s\n",
           raw_done ? "DONE" : "BUSY",
           low_done ? "DONE" : "BUSY",
           high_done ? "DONE" : "BUSY",
           combine_done ? "DONE" : "BUSY",
           final_done ? "DONE" : "BUSY");

    if (first_valid_phase >= 0)
        printf("FIRST VALID DATA PHASE: %s\n",
               phase_name((uint)first_valid_phase));
    else
        printf("FIRST VALID DATA PHASE: NONE\n");

    const bool pass =
        reset_clock_ok && observer_complete && address_ok && lane_ok &&
        read_ok && raw_done && low_done && high_done &&
        combine_done && final_done && first_valid_phase >= 0;

    printf("RESULT: %s (expected data=%04x, matching phases=%u)\n",
           pass ? "PASS" : "FAIL", ISSUE106_RESET_WORD, phase_matches);
    printf("NOTE: one-shot 1MHz empirical timing probe; #108 must classify ");
    printf("the observed phase against Intel >=2MHz fixed-READY timing.\n");

    while (true) tight_loop_contents();
}
