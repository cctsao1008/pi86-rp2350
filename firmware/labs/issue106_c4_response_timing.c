/*
 * Issue #106 Phase C4: one-shot end-to-end physical read response timing lab.
 *
 * This experiment intentionally services only the architectural reset-vector
 * WORD read at FFFF0h. The raw CPU-visible SRAM word is EB FE (JMP $), stored
 * little-endian as FEEBh. The dataplane is:
 *
 *   physical T1 snapshot
 *     -> scattered A15..A0 extraction
 *     -> 64 KiB-aligned byte backing pointer
 *     -> DMA raw SRAM read
 *     -> PIO LUT-pointer construction
 *     -> DMA 16-bit-to-scattered-GPIO lookup
 *     -> PIO AD-bus drive/release
 *
 * An independent observer records T1/AF/R1/F1/R2/F2/R3. The first phase whose
 * AD bus decodes to FEEBh is reported as the empirical response phase.
 *
 * This is deliberately one-shot so a timing failure cannot leave a stale
 * responder driving later processor cycles.
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
#define ISSUE106_OBSERVER_WORDS 7u
#define ISSUE106_RESET_WORD 0xfeeBu

static uint8_t backing[65536] __attribute__((aligned(65536)));
static uint32_t response_lut[65536] __attribute__((aligned(262144)));
static uint32_t frontend_words[ISSUE106_FRONTEND_WORDS];
static uint32_t observer_words[ISSUE106_OBSERVER_WORDS];

static const uint8_t ad_gpio[16] = {
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

static uint32_t encode_data_bitmap(uint16_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if ((value >> bit) & 1u) encoded |= 1u << ad_gpio[bit];
    }
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

    /* Reset-vector ROM image: EB FE = JMP short -2, little-endian FEEBh. */
    backing[0xfff0u] = 0xebu;
    backing[0xfff1u] = 0xfeu;

    for (uint value = 0u; value < 65536u; ++value)
        response_lut[value] = encode_data_bitmap((uint16_t)value);
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
    pio_sm_set_enabled(pio, sm, true);
}

static void init_lut_pointer(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c4_lut_pointer_program_get_default_config(offset);
    sm_config_set_in_shift(&c, false, false, 32u);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);

    const uint32_t lut_hi14 =
        ((uint32_t)(uintptr_t)response_lut >> 18) & 0x3fffu;
    pio_sm_put_blocking(pio, sm, lut_hi14);
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
    pio_sm_put_blocking(pio, sm, RP86_PROCESSOR_AD_BUS_MASK);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue106_c4_observer_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
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
        "T1", "AF", "R1", "F1", "R2", "F2", "R3"
    };
    return index < ISSUE106_OBSERVER_WORDS ? names[index] : "?";
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue106 C4 end-to-end read response timing lab\n");
    printf("scope: T1 -> raw SRAM -> encode LUT -> scattered AD drive\n");
    printf("processor clock: %u Hz (empirical; Intel compliance not claimed)\n",
           ISSUE106_PROCESSOR_HZ);

    const uintptr_t backing_addr = (uintptr_t)backing;
    const uintptr_t lut_addr = (uintptr_t)response_lut;
    hard_assert((backing_addr & 0xffffu) == 0u);
    hard_assert((lut_addr & 0x3ffffu) == 0u);

    prepare_memory();
    prepare_header_safe();

    PIO service_pio = pio0;
    const uint frontend_sm = pio_claim_unused_sm(service_pio, true);
    const uint lut_pointer_sm = pio_claim_unused_sm(service_pio, true);
    const uint responder_sm = pio_claim_unused_sm(service_pio, true);

    const uint frontend_offset =
        pio_add_program(service_pio, &issue106_c4_frontend_program);
    const uint lut_pointer_offset =
        pio_add_program(service_pio, &issue106_c4_lut_pointer_program);
    const uint responder_offset =
        pio_add_program(service_pio, &issue106_c4_responder_program);

    init_frontend(service_pio, frontend_sm, frontend_offset);
    init_lut_pointer(service_pio, lut_pointer_sm, lut_pointer_offset);
    init_responder(service_pio, responder_sm, responder_offset);
    build_frontend_stream(frontend_offset);

    const uint frontend_tx_dma = dma_claim_unused_channel(true);
    dma_channel_config frontend_tx_cfg =
        dma_channel_get_default_config(frontend_tx_dma);
    channel_config_set_transfer_data_size(&frontend_tx_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&frontend_tx_cfg, true);
    channel_config_set_write_increment(&frontend_tx_cfg, false);
    channel_config_set_dreq(
        &frontend_tx_cfg, pio_get_dreq(service_pio, frontend_sm, true));
    channel_config_set_high_priority(&frontend_tx_cfg, true);
    dma_channel_configure(frontend_tx_dma, &frontend_tx_cfg,
                          &service_pio->txf[frontend_sm],
                          frontend_words, ISSUE106_FRONTEND_WORDS, false);

    /* Raw backing read. A 32-bit fetch is safe at FFFF0h; low 16 bits are data. */
    const uint raw_data_dma = dma_claim_unused_channel(true);
    dma_channel_config raw_data_cfg =
        dma_channel_get_default_config(raw_data_dma);
    channel_config_set_transfer_data_size(&raw_data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&raw_data_cfg, false);
    channel_config_set_write_increment(&raw_data_cfg, false);
    channel_config_set_dreq(
        &raw_data_cfg, pio_get_dreq(service_pio, lut_pointer_sm, true));
    channel_config_set_high_priority(&raw_data_cfg, true);
    dma_channel_configure(raw_data_dma, &raw_data_cfg,
                          &service_pio->txf[lut_pointer_sm], backing,
                          1u, false);

    const uint frontend_control_dma = dma_claim_unused_channel(true);
    dma_channel_config frontend_control_cfg =
        dma_channel_get_default_config(frontend_control_dma);
    channel_config_set_transfer_data_size(&frontend_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&frontend_control_cfg, false);
    channel_config_set_write_increment(&frontend_control_cfg, false);
    channel_config_set_dreq(
        &frontend_control_cfg,
        pio_get_dreq(service_pio, frontend_sm, false));
    channel_config_set_high_priority(&frontend_control_cfg, true);
    dma_channel_configure(
        frontend_control_dma, &frontend_control_cfg,
        &dma_channel_hw_addr(raw_data_dma)->al3_read_addr_trig,
        &service_pio->rxf[frontend_sm], 1u, false);

    /* Builder RX emits response_lut[data] absolute pointer. */
    const uint lut_data_dma = dma_claim_unused_channel(true);
    dma_channel_config lut_data_cfg =
        dma_channel_get_default_config(lut_data_dma);
    channel_config_set_transfer_data_size(&lut_data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&lut_data_cfg, false);
    channel_config_set_write_increment(&lut_data_cfg, false);
    channel_config_set_dreq(
        &lut_data_cfg, pio_get_dreq(service_pio, responder_sm, true));
    channel_config_set_high_priority(&lut_data_cfg, true);
    dma_channel_configure(lut_data_dma, &lut_data_cfg,
                          &service_pio->txf[responder_sm], response_lut,
                          1u, false);

    const uint lut_control_dma = dma_claim_unused_channel(true);
    dma_channel_config lut_control_cfg =
        dma_channel_get_default_config(lut_control_dma);
    channel_config_set_transfer_data_size(&lut_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&lut_control_cfg, false);
    channel_config_set_write_increment(&lut_control_cfg, false);
    channel_config_set_dreq(
        &lut_control_cfg,
        pio_get_dreq(service_pio, lut_pointer_sm, false));
    channel_config_set_high_priority(&lut_control_cfg, true);
    dma_channel_configure(
        lut_control_dma, &lut_control_cfg,
        &dma_channel_hw_addr(lut_data_dma)->al3_read_addr_trig,
        &service_pio->rxf[lut_pointer_sm], 1u, false);

    PIO observer_pio = pio1;
    const uint observer_sm = pio_claim_unused_sm(observer_pio, true);
    const uint observer_offset =
        pio_add_program(observer_pio, &issue106_c4_observer_program);
    init_observer(observer_pio, observer_sm, observer_offset);

    const uint observer_dma = dma_claim_unused_channel(true);
    dma_channel_config observer_cfg =
        dma_channel_get_default_config(observer_dma);
    channel_config_set_transfer_data_size(&observer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&observer_cfg, false);
    channel_config_set_write_increment(&observer_cfg, true);
    channel_config_set_dreq(
        &observer_cfg, pio_get_dreq(observer_pio, observer_sm, false));
    channel_config_set_high_priority(&observer_cfg, true);
    dma_channel_configure(observer_dma, &observer_cfg,
                          observer_words, &observer_pio->rxf[observer_sm],
                          ISSUE106_OBSERVER_WORDS, false);

    dma_start_channel_mask(
        (1u << frontend_tx_dma) |
        (1u << frontend_control_dma) |
        (1u << lut_control_dma) |
        (1u << observer_dma));

    const uint clock_sm = pio_claim_unused_sm(observer_pio, true);
    const uint clock_offset =
        pio_add_program(observer_pio, &rp86_free_running_clock_program);
    init_clock(observer_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool observer_complete = wait_dma_idle(observer_dma, deadline);

    /* Immediately bound the experiment after the first observed bus cycle. */
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    pio_sm_set_enabled(observer_pio, clock_sm, false);

    /* Force the header back to a safe state regardless of responder progress. */
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

    printf("BACKING BASE: %08" PRIxPTR "\n", backing_addr);
    printf("LUT BASE:     %08" PRIxPTR "\n", lut_addr);
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

    const bool raw_dma_done = !dma_channel_is_busy(raw_data_dma);
    const bool lut_dma_done = !dma_channel_is_busy(lut_data_dma);
    const bool control_done = !dma_channel_is_busy(frontend_control_dma) &&
                              !dma_channel_is_busy(lut_control_dma);

    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n", address_ok ? "PASS" : "FAIL");
    printf("FIRST CYCLE WORD MEMREAD: %s\n",
           lane_ok && read_ok ? "PASS" : "FAIL");
    printf("DMA PIPELINE: raw=%s lut=%s control=%s\n",
           raw_dma_done ? "DONE" : "BUSY",
           lut_dma_done ? "DONE" : "BUSY",
           control_done ? "DONE" : "BUSY");

    if (first_valid_phase >= 0)
        printf("FIRST VALID DATA PHASE: %s\n",
               phase_name((uint)first_valid_phase));
    else
        printf("FIRST VALID DATA PHASE: NONE\n");

    const bool pass =
        reset_clock_ok && observer_complete && address_ok && lane_ok &&
        read_ok && raw_dma_done && lut_dma_done && control_done &&
        first_valid_phase >= 0;

    printf("RESULT: %s (expected data=%04x, matching phases=%u)\n",
           pass ? "PASS" : "FAIL", ISSUE106_RESET_WORD, phase_matches);
    printf("NOTE: one-shot 1MHz empirical timing probe; #108 must decide whether ");
    printf("the observed phase satisfies Intel >=2MHz fixed-READY timing.\n");

    while (true) tight_loop_contents();
}
