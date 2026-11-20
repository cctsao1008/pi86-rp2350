/*
 * Issue #106 C5 tail-latency decomposition.
 *
 * T0 (ISSUE106_TAIL_MODE=0):
 *   T1-qualified known encoded data -> one DMA transfer -> responder.
 *
 * T1 (ISSUE106_TAIL_MODE=1):
 *   T1-qualified known SRAM pointer -> control DMA -> SRAM DMA -> responder.
 *
 * These probes keep the same RESET/T1 qualification, physical observer, and
 * responder as C5A/B. They isolate how much of the 6 MHz R3 miss belongs to
 * the tail versus the address-translation front half.
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
#include "issue106_c5_tail_timing.pio.h"
#include "intel_8086_clock.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#ifndef ISSUE106_PROCESSOR_HZ
#define ISSUE106_PROCESSOR_HZ 6000000u
#endif

#ifndef ISSUE106_TAIL_MODE
#define ISSUE106_TAIL_MODE 0
#endif

#if ISSUE106_TAIL_MODE != 0 && ISSUE106_TAIL_MODE != 1
#error "ISSUE106_TAIL_MODE must be 0 (direct data) or 1 (fixed SRAM pointer)"
#endif

#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_OBSERVER_WORDS 18u
#define ISSUE106_ACCEPTANCE_LAST_PHASE 4u /* R2 */
#define ISSUE106_RESET_WORD 0xfeeBu

static uint32_t encoded_word;
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
    for (uint bit = 0u; bit < 16u; ++bit)
        if ((value >> bit) & 1u) encoded |= 1u << ad_gpio[bit];
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

static void init_trigger(PIO pio, uint sm, uint offset, uint32_t value) {
    pio_sm_config c =
        issue106_c5_tail_trigger_program_get_default_config(offset);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_put_blocking(pio, sm, value);
    pio_sm_set_enabled(pio, sm, false);
}

static void init_responder(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c5_tail_responder_program_get_default_config(offset);
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
        issue106_c5_tail_observer_program_get_default_config(offset);
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

    encoded_word = encode_word(ISSUE106_RESET_WORD);
    for (uint i = 0u; i < ISSUE106_OBSERVER_WORDS; ++i)
        observer_words[i] = 0u;

    printf("issue106 C5-T%u tail latency lab\n", ISSUE106_TAIL_MODE);
#if ISSUE106_TAIL_MODE == 0
    printf("scope: T1 -> direct encoded-data DMA -> responder\n");
#else
    printf("scope: T1 -> fixed pointer control DMA -> SRAM DMA -> responder\n");
#endif
    printf("processor clock: %u Hz (empirical; Intel compliance not claimed)\n",
           ISSUE106_PROCESSOR_HZ);

    prepare_header_safe();

    PIO trigger_pio = pio0;
    const uint trigger_sm = pio_claim_unused_sm(trigger_pio, true);
    const uint trigger_offset =
        pio_add_program(trigger_pio, &issue106_c5_tail_trigger_program);

#if ISSUE106_TAIL_MODE == 0
    const uint32_t trigger_value = encoded_word;
#else
    const uint32_t trigger_value = (uint32_t)(uintptr_t)&encoded_word;
#endif
    init_trigger(trigger_pio, trigger_sm, trigger_offset, trigger_value);

    PIO io_pio = pio2;
    const uint responder_sm = pio_claim_unused_sm(io_pio, true);
    const uint observer_sm = pio_claim_unused_sm(io_pio, true);
    const uint responder_offset =
        pio_add_program(io_pio, &issue106_c5_tail_responder_program);
    const uint observer_offset =
        pio_add_program(io_pio, &issue106_c5_tail_observer_program);
    init_responder(io_pio, responder_sm, responder_offset);
    init_observer(io_pio, observer_sm, observer_offset);

    PIO clock_pio = pio1;
    const uint clock_sm = pio_claim_unused_sm(clock_pio, true);
    const uint clock_offset =
        pio_add_program(clock_pio, &rp86_intel_8086_clock_program);

    const uint trigger_dma = dma_claim_unused_channel(true);
#if ISSUE106_TAIL_MODE == 1
    const uint raw_data_dma = dma_claim_unused_channel(true);
#endif
    const uint observer_dma = dma_claim_unused_channel(true);

#if ISSUE106_TAIL_MODE == 0
    dma_channel_config trigger_cfg =
        dma_channel_get_default_config(trigger_dma);
    channel_config_set_transfer_data_size(&trigger_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&trigger_cfg, false);
    channel_config_set_write_increment(&trigger_cfg, false);
    channel_config_set_dreq(
        &trigger_cfg, pio_get_dreq(trigger_pio, trigger_sm, false));
    channel_config_set_high_priority(&trigger_cfg, true);
    dma_channel_configure(
        trigger_dma, &trigger_cfg,
        &io_pio->txf[responder_sm],
        &trigger_pio->rxf[trigger_sm], 1u, false);
#else
    dma_channel_config raw_cfg =
        dma_channel_get_default_config(raw_data_dma);
    channel_config_set_transfer_data_size(&raw_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&raw_cfg, false);
    channel_config_set_write_increment(&raw_cfg, false);
    channel_config_set_dreq(
        &raw_cfg, pio_get_dreq(io_pio, responder_sm, true));
    channel_config_set_high_priority(&raw_cfg, true);
    dma_channel_configure(
        raw_data_dma, &raw_cfg,
        &io_pio->txf[responder_sm], &encoded_word, 1u, false);

    dma_channel_config trigger_cfg =
        dma_channel_get_default_config(trigger_dma);
    channel_config_set_transfer_data_size(&trigger_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&trigger_cfg, false);
    channel_config_set_write_increment(&trigger_cfg, false);
    channel_config_set_dreq(
        &trigger_cfg, pio_get_dreq(trigger_pio, trigger_sm, false));
    channel_config_set_high_priority(&trigger_cfg, true);
    dma_channel_configure(
        trigger_dma, &trigger_cfg,
        &dma_channel_hw_addr(raw_data_dma)->al3_read_addr_trig,
        &trigger_pio->rxf[trigger_sm], 1u, false);
#endif

    dma_channel_config observer_cfg =
        dma_channel_get_default_config(observer_dma);
    channel_config_set_transfer_data_size(&observer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&observer_cfg, false);
    channel_config_set_write_increment(&observer_cfg, true);
    channel_config_set_dreq(
        &observer_cfg, pio_get_dreq(io_pio, observer_sm, false));
    channel_config_set_high_priority(&observer_cfg, true);
    dma_channel_configure(
        observer_dma, &observer_cfg,
        observer_words, &io_pio->rxf[observer_sm],
        ISSUE106_OBSERVER_WORDS, false);

    dma_channel_start(trigger_dma);
    dma_channel_start(observer_dma);

    pio_sm_restart(trigger_pio, trigger_sm);
    pio_sm_exec(trigger_pio, trigger_sm, pio_encode_jmp(trigger_offset));
    pio_sm_restart(io_pio, observer_sm);
    pio_sm_exec(io_pio, observer_sm, pio_encode_jmp(observer_offset));
    pio_sm_set_enabled(trigger_pio, trigger_sm, true);
    pio_sm_set_enabled(io_pio, observer_sm, true);

    init_clock(clock_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t observer_deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool observer_complete =
        wait_dma_idle(observer_dma, observer_deadline);
    const uint32_t observer_remaining =
        dma_channel_hw_addr(observer_dma)->transfer_count & 0x0fffffffu;

    const uint64_t settle_deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool trigger_done = wait_dma_idle(trigger_dma, settle_deadline);
#if ISSUE106_TAIL_MODE == 1
    const bool raw_done = wait_dma_idle(raw_data_dma, settle_deadline);
#endif

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

    printf("OBSERVER: %s (%u/%u words captured)\n",
           observer_complete ? "COMPLETE" : "TIMEOUT",
           ISSUE106_OBSERVER_WORDS - observer_remaining,
           ISSUE106_OBSERVER_WORDS);
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n", address_ok ? "PASS" : "FAIL");
    printf("FIRST CYCLE WORD MEMREAD: %s\n",
           lane_ok && read_ok ? "PASS" : "FAIL");
#if ISSUE106_TAIL_MODE == 0
    printf("PIPELINE: trigger=%s\n", trigger_done ? "DONE" : "TIMEOUT");
#else
    printf("PIPELINE: ptrctl=%s sram=%s\n",
           trigger_done ? "DONE" : "TIMEOUT",
           raw_done ? "DONE" : "TIMEOUT");
    printf("FIXED SRAM PTR: actual=%08" PRIx32 " expected=%08" PRIx32 "\n",
           dma_channel_hw_addr(raw_data_dma)->read_addr,
           (uint32_t)(uintptr_t)&encoded_word);
#endif

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

    bool pass =
        reset_clock_ok && observer_complete && address_ok && lane_ok &&
        read_ok && trigger_done && first_valid_phase >= 0;
#if ISSUE106_TAIL_MODE == 1
    pass = pass && raw_done;
#endif

    printf("RESULT: %s (expected data=%04x, valid matches=%u)\n",
           pass ? "PASS" : "FAIL", ISSUE106_RESET_WORD, valid_matches);
    printf("NOTE: diagnostic tail decomposition only.\n");

    while (true) tight_loop_contents();
}
