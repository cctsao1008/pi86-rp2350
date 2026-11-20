/*
 * Issue #106 Phase C5A: speed-first one-shot dynamic WORD-read dataplane.
 *
 * Goal:
 *   Prove whether a fixed-latency parallel frontend can service one physical
 *   Intel 8086 WORD read at the 6 MHz engineering baseline with READY fixed.
 *
 * This intentionally removes the C4 serial scattered-address repack and its
 * DMA-fed microinstruction stream from the realtime path.
 *
 * Active path:
 *   two parallel 14-bit raw-GPIO frontends
 *       -> two 16K address LUT lookups
 *       -> DMA sniffer SUM => encoded-backing pointer
 *       -> one SRAM read
 *       -> pre-encoded scattered-GPIO response
 *       -> responder PIO
 *
 * The pre-encoded backing is a speed-first laboratory representation, not the
 * final canonical memory format. It isolates address-translation + SRAM +
 * response timing before byte lanes, writes, and storage-density work.
 *
 * M33 configures and verifies only. It does not service the active cycle.
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
#include "issue106_c5_fast_read.pio.h"
#include "intel_8086_clock.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#ifndef ISSUE106_PROCESSOR_HZ
#define ISSUE106_PROCESSOR_HZ 6000000u
#endif

#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_ADDR_LUT_ENTRIES 16384u
#define ISSUE106_ENCODED_WORDS 32768u
#define ISSUE106_OBSERVER_WORDS 18u
#define ISSUE106_ACCEPTANCE_LAST_PHASE 4u /* R2 */
#define ISSUE106_RESET_WORD 0xfeeBu
#define ISSUE106_DMA_SNIFF_SUM_MODE 0x0fu

/*
 * One 128 KiB object gives us two naturally adjacent 64 KiB-aligned LUTs.
 * Each LUT has 16384 uint32_t entries, exactly 64 KiB.
 */
static uint32_t addr_lut[2][ISSUE106_ADDR_LUT_ENTRIES]
    __attribute__((aligned(65536)));

/*
 * Speed-first representation: one 32-bit pre-encoded GPIO bitmap per logical
 * 16-bit WORD. A 64 KiB logical byte window therefore consumes 128 KiB.
 */
static uint32_t encoded_backing[ISSUE106_ENCODED_WORDS];

static uint32_t address_parts[2];
static uint32_t sniff_sink;
static uint32_t observer_words[ISSUE106_OBSERVER_WORDS];

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

static uint32_t encode_word(uint16_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if ((value >> bit) & 1u) encoded |= 1u << ad_gpio[bit];
    }
    return encoded;
}

static const char *phase_name(uint index) {
    static const char *const names[ISSUE106_OBSERVER_WORDS] = {
        "T1", "AF",
        "R1", "F1", "R2", "F2", "R3", "F3", "R4", "F4",
        "R5", "F5", "R6", "F6", "R7", "F7", "R8", "F8"
    };
    return index < ISSUE106_OBSERVER_WORDS ? names[index] : "?";
}

static void prepare_memory(void) {
    const uintptr_t backing_base = (uintptr_t)encoded_backing;

    for (uint i = 0u; i < ISSUE106_ENCODED_WORDS; ++i)
        encoded_backing[i] = encode_word(0x9090u);

    encoded_backing[0xfff0u >> 1u] = encode_word(ISSUE106_RESET_WORD);

    /*
     * Each table sees one contiguous 14-GPIO half of GPIO0..27. Decode only
     * the address bits physically present in that half. The two contributions
     * are disjoint logical address bits, so arithmetic SUM equals OR.
     *
     * A logical even-byte address A becomes a uint32_t word-pointer offset:
     *     word_index = A >> 1
     *     byte_offset = word_index * 4 = (A & ~1) << 1
     *
     * Table 0 also carries the absolute encoded_backing base.
     */
    for (uint index = 0u; index < ISSUE106_ADDR_LUT_ENTRIES; ++index) {
        const uint32_t raw_lo = index;
        const uint32_t raw_hi = index << 14u;
        const uint16_t addr_lo = decode_ad(raw_lo);
        const uint16_t addr_hi = decode_ad(raw_hi);
        const uint32_t offset_lo =
            ((uint32_t)(addr_lo & 0xfffeu)) << 1u;
        const uint32_t offset_hi =
            ((uint32_t)(addr_hi & 0xfffeu)) << 1u;

        addr_lut[0][index] = (uint32_t)backing_base + offset_lo;
        addr_lut[1][index] = offset_hi;
    }

    address_parts[0] = 0u;
    address_parts[1] = 0u;
    sniff_sink = 0u;
    for (uint i = 0u; i < ISSUE106_OBSERVER_WORDS; ++i)
        observer_words[i] = 0u;
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

static void init_frontend(PIO pio, uint sm, uint offset,
                          uint in_base, uintptr_t lut_base) {
    pio_sm_config c =
        issue106_c5_addr_frontend_program_get_default_config(offset);
    sm_config_set_in_pins(&c, in_base);
    sm_config_set_in_shift(&c, false, false, 32u);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);

    hard_assert((lut_base & 0xffffu) == 0u);
    pio_sm_put_blocking(pio, sm, (uint32_t)(lut_base >> 16u));
    pio_sm_set_enabled(pio, sm, false);
}

static void init_responder(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c5_responder_program_get_default_config(offset);
    sm_config_set_out_pins(&c, 0u, 28u);
    sm_config_set_out_shift(&c, true, false, 32u);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);

    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(pio, ad_gpio[bit]);

    pio_sm_set_consecutive_pindirs(pio, sm, 0u, 28u, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c5_observer_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void init_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        rp86_intel_8086_clock_program_get_default_config(offset);
    const float divider =
        (float)clock_get_hz(clk_sys) / (5.0f * (float)ISSUE106_PROCESSOR_HZ);
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

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue106 C5A fast one-shot WORD-read timing lab\n");
    printf("scope: parallel 14b LUT frontend -> SRAM -> preencoded AD drive\n");
    printf("processor clock: %u Hz (empirical; Intel compliance not claimed)\n",
           ISSUE106_PROCESSOR_HZ);

    const uintptr_t lut0_base = (uintptr_t)&addr_lut[0][0];
    const uintptr_t lut1_base = (uintptr_t)&addr_lut[1][0];
    const uintptr_t backing_base = (uintptr_t)encoded_backing;

    hard_assert((lut0_base & 0xffffu) == 0u);
    hard_assert((lut1_base & 0xffffu) == 0u);
    hard_assert(lut1_base - lut0_base == 65536u);

    prepare_memory();
    prepare_header_safe();

    PIO frontend_pio = pio0;
    const uint frontend0_sm = pio_claim_unused_sm(frontend_pio, true);
    const uint frontend1_sm = pio_claim_unused_sm(frontend_pio, true);
    const uint frontend_offset =
        pio_add_program(frontend_pio, &issue106_c5_addr_frontend_program);
    init_frontend(frontend_pio, frontend0_sm, frontend_offset, 0u, lut0_base);
    init_frontend(frontend_pio, frontend1_sm, frontend_offset, 14u, lut1_base);

    PIO io_pio = pio2;
    const uint responder_sm = pio_claim_unused_sm(io_pio, true);
    const uint observer_sm = pio_claim_unused_sm(io_pio, true);
    const uint responder_offset =
        pio_add_program(io_pio, &issue106_c5_responder_program);
    const uint observer_offset =
        pio_add_program(io_pio, &issue106_c5_observer_program);
    init_responder(io_pio, responder_sm, responder_offset);
    init_observer(io_pio, observer_sm, observer_offset);

    PIO clock_pio = pio1;
    const uint clock_sm = pio_claim_unused_sm(clock_pio, true);
    const uint clock_offset =
        pio_add_program(clock_pio, &rp86_intel_8086_clock_program);

    const uint low_control_dma = dma_claim_unused_channel(true);
    const uint low_lookup_dma = dma_claim_unused_channel(true);
    const uint high_control_dma = dma_claim_unused_channel(true);
    const uint high_lookup_dma = dma_claim_unused_channel(true);
    const uint combine_dma = dma_claim_unused_channel(true);
    const uint pointer_dma = dma_claim_unused_channel(true);
    const uint raw_data_dma = dma_claim_unused_channel(true);
    const uint observer_dma = dma_claim_unused_channel(true);

    dma_channel_config low_lookup_cfg =
        dma_channel_get_default_config(low_lookup_dma);
    channel_config_set_transfer_data_size(&low_lookup_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&low_lookup_cfg, false);
    channel_config_set_write_increment(&low_lookup_cfg, false);
    channel_config_set_dreq(&low_lookup_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&low_lookup_cfg, high_control_dma);
    channel_config_set_high_priority(&low_lookup_cfg, true);
    dma_channel_configure(low_lookup_dma, &low_lookup_cfg,
                          &address_parts[0], &addr_lut[0][0], 1u, false);

    dma_channel_config low_control_cfg =
        dma_channel_get_default_config(low_control_dma);
    channel_config_set_transfer_data_size(&low_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&low_control_cfg, false);
    channel_config_set_write_increment(&low_control_cfg, false);
    channel_config_set_dreq(
        &low_control_cfg,
        pio_get_dreq(frontend_pio, frontend0_sm, false));
    channel_config_set_chain_to(&low_control_cfg, low_lookup_dma);
    channel_config_set_high_priority(&low_control_cfg, true);
    dma_channel_configure(
        low_control_dma, &low_control_cfg,
        &dma_channel_hw_addr(low_lookup_dma)->read_addr,
        &frontend_pio->rxf[frontend0_sm], 1u, false);

    dma_channel_config high_lookup_cfg =
        dma_channel_get_default_config(high_lookup_dma);
    channel_config_set_transfer_data_size(&high_lookup_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&high_lookup_cfg, false);
    channel_config_set_write_increment(&high_lookup_cfg, false);
    channel_config_set_dreq(&high_lookup_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&high_lookup_cfg, combine_dma);
    channel_config_set_high_priority(&high_lookup_cfg, true);
    dma_channel_configure(high_lookup_dma, &high_lookup_cfg,
                          &address_parts[1], &addr_lut[1][0], 1u, false);

    dma_channel_config high_control_cfg =
        dma_channel_get_default_config(high_control_dma);
    channel_config_set_transfer_data_size(&high_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&high_control_cfg, false);
    channel_config_set_write_increment(&high_control_cfg, false);
    channel_config_set_dreq(
        &high_control_cfg,
        pio_get_dreq(frontend_pio, frontend1_sm, false));
    channel_config_set_chain_to(&high_control_cfg, high_lookup_dma);
    channel_config_set_high_priority(&high_control_cfg, true);
    dma_channel_configure(
        high_control_dma, &high_control_cfg,
        &dma_channel_hw_addr(high_lookup_dma)->read_addr,
        &frontend_pio->rxf[frontend1_sm], 1u, false);

    dma_channel_config raw_cfg =
        dma_channel_get_default_config(raw_data_dma);
    channel_config_set_transfer_data_size(&raw_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&raw_cfg, false);
    channel_config_set_write_increment(&raw_cfg, false);
    channel_config_set_dreq(
        &raw_cfg, pio_get_dreq(io_pio, responder_sm, true));
    channel_config_set_high_priority(&raw_cfg, true);
    dma_channel_configure(raw_data_dma, &raw_cfg,
                          &io_pio->txf[responder_sm],
                          &encoded_backing[0], 1u, false);

    dma_channel_config pointer_cfg =
        dma_channel_get_default_config(pointer_dma);
    channel_config_set_transfer_data_size(&pointer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&pointer_cfg, false);
    channel_config_set_write_increment(&pointer_cfg, false);
    channel_config_set_dreq(&pointer_cfg, DREQ_FORCE);
    channel_config_set_high_priority(&pointer_cfg, true);
    dma_channel_configure(
        pointer_dma, &pointer_cfg,
        &dma_channel_hw_addr(raw_data_dma)->al3_read_addr_trig,
        &dma_hw->sniff_data, 1u, false);

    dma_channel_config combine_cfg =
        dma_channel_get_default_config(combine_dma);
    channel_config_set_transfer_data_size(&combine_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&combine_cfg, true);
    channel_config_set_write_increment(&combine_cfg, false);
    channel_config_set_dreq(&combine_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&combine_cfg, pointer_dma);
    channel_config_set_sniff_enable(&combine_cfg, true);
    channel_config_set_high_priority(&combine_cfg, true);
    dma_channel_configure(combine_dma, &combine_cfg,
                          &sniff_sink, address_parts, 2u, false);

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
    dma_sniffer_enable(combine_dma, ISSUE106_DMA_SNIFF_SUM_MODE, false);

    /*
     * The chained sequence starts at low_control. high_control is started by
     * low_lookup, and the remaining address/read path advances entirely by
     * DMA chaining or the dynamic read-address trigger.
     */
    dma_channel_start(low_control_dma);
    dma_channel_start(observer_dma);

    pio_sm_restart(frontend_pio, frontend0_sm);
    pio_sm_exec(frontend_pio, frontend0_sm,
                pio_encode_jmp(frontend_offset));
    pio_sm_restart(frontend_pio, frontend1_sm);
    pio_sm_exec(frontend_pio, frontend1_sm,
                pio_encode_jmp(frontend_offset));
    pio_sm_restart(io_pio, observer_sm);
    pio_sm_exec(io_pio, observer_sm,
                pio_encode_jmp(observer_offset));

    pio_sm_set_enabled(frontend_pio, frontend0_sm, true);
    pio_sm_set_enabled(frontend_pio, frontend1_sm, true);
    pio_sm_set_enabled(io_pio, observer_sm, true);

    init_clock(clock_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t observer_deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool observer_complete = wait_dma_idle(observer_dma, observer_deadline);
    const uint32_t observer_remaining =
        dma_channel_hw_addr(observer_dma)->transfer_count & 0x0fffffffu;

    const uint64_t settle_deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool low_control_done =
        wait_dma_idle(low_control_dma, settle_deadline);
    const bool low_lookup_done =
        wait_dma_idle(low_lookup_dma, settle_deadline);
    const bool high_control_done =
        wait_dma_idle(high_control_dma, settle_deadline);
    const bool high_lookup_done =
        wait_dma_idle(high_lookup_dma, settle_deadline);
    const bool combine_done =
        wait_dma_idle(combine_dma, settle_deadline);
    const bool pointer_done =
        wait_dma_idle(pointer_dma, settle_deadline);
    const bool raw_done =
        wait_dma_idle(raw_data_dma, settle_deadline);

    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    pio_sm_set_enabled(clock_pio, clock_sm, false);

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
    int first_late_phase = -1;
    uint valid_matches = 0u;
    uint late_matches = 0u;

    printf("ADDR LUT0 BASE: %08" PRIxPTR "\n", lut0_base);
    printf("ADDR LUT1 BASE: %08" PRIxPTR "\n", lut1_base);
    printf("ENCODED BACKING: %08" PRIxPTR "\n", backing_base);
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
        if (!match) continue;
        if (i <= ISSUE106_ACCEPTANCE_LAST_PHASE) {
            ++valid_matches;
            if (first_valid_phase < 0) first_valid_phase = (int)i;
        } else {
            ++late_matches;
            if (first_late_phase < 0) first_late_phase = (int)i;
        }
    }

    const uint32_t final_pointer = dma_sniffer_get_data_accumulator();
    const uintptr_t expected_pointer =
        (uintptr_t)&encoded_backing[0xfff0u >> 1u];

    printf("OBSERVER: %s (%u/%u words captured)\n",
           observer_complete ? "COMPLETE" : "TIMEOUT",
           ISSUE106_OBSERVER_WORDS - observer_remaining,
           ISSUE106_OBSERVER_WORDS);
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n", address_ok ? "PASS" : "FAIL");
    printf("FIRST CYCLE WORD MEMREAD: %s\n",
           lane_ok && read_ok ? "PASS" : "FAIL");
    printf("PIPELINE: lowctl=%s low=%s highctl=%s high=%s "
           "sum=%s ptr=%s sram=%s\n",
           low_control_done ? "DONE" : "TIMEOUT",
           low_lookup_done ? "DONE" : "TIMEOUT",
           high_control_done ? "DONE" : "TIMEOUT",
           high_lookup_done ? "DONE" : "TIMEOUT",
           combine_done ? "DONE" : "TIMEOUT",
           pointer_done ? "DONE" : "TIMEOUT",
           raw_done ? "DONE" : "TIMEOUT");
    printf("ADDR PARTS: low=%08" PRIx32 " high=%08" PRIx32 "\n",
           address_parts[0], address_parts[1]);
    printf("SRAM PTR: actual=%08" PRIx32 " expected=%08" PRIxPTR "\n",
           final_pointer, expected_pointer);

    if (first_valid_phase >= 0)
        printf("FIRST VALID DATA PHASE: %s\n",
               phase_name((uint)first_valid_phase));
    else
        printf("FIRST VALID DATA PHASE: NONE\n");

    if (first_late_phase >= 0)
        printf("FIRST LATE DATA PHASE: %s (%u late matches)\n",
               phase_name((uint)first_late_phase), late_matches);
    else
        printf("FIRST LATE DATA PHASE: NONE\n");

    const bool pass =
        reset_clock_ok && observer_complete && address_ok && lane_ok &&
        read_ok && low_control_done && low_lookup_done &&
        high_control_done && high_lookup_done && combine_done &&
        pointer_done && raw_done &&
        final_pointer == (uint32_t)expected_pointer &&
        first_valid_phase >= 0;

    printf("RESULT: %s (expected data=%04x, valid matches=%u)\n",
           pass ? "PASS" : "FAIL", ISSUE106_RESET_WORD, valid_matches);
    printf("NOTE: C5A speed-first representation; not canonical memory format.\n");

    while (true) tight_loop_contents();
}
