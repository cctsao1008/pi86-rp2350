/*
 * PC1-C0C1-A2 current-address selector with same-cycle AD response.
 *
 * Every stage starts with a fresh clock-only RESET qualification. PIO1 then
 * captures the first physical early-T1 key, scans a DMA-fed internal-SRAM
 * table, gates the selected response while ASTB is still high, drives the
 * scattered AD pins after ASTB fall, and releases them at the accepted H2
 * boundary. The M33 never participates in the current-cycle decision.
 *
 * Hit stages place FFFF0 last at depths 1/4/8/16/32. A final depth-32 miss
 * proves that exhausting the table produces no drive authorization. PIO0
 * independently records the first address and AF/R1/F1/R2/F2/R3 snapshots.
 * PIO2 owns CLK so PIO1 can release its entire PINDIRS group without affecting
 * the external CPU clock.
 *
 * This remains a one-cycle feasibility instrument. It does not yet rewind or
 * refill a general ROM table across arbitrary CPU execution.
 */

#include <stdbool.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "pc1b_first_cycle_phase_capture.pio.h"
#include "pc1c_arbitrary_rom_response.pio.h"
#include "perf_continuous_clock.pio.h"
#include "v30/v30_pins.h"

#define C0C1A2_V30_HZ                    300000u
#define RESET_CLOCKS                          20u
#define SIGNAL_TIMEOUT_CLOCKS                 64u
#define STAGE_TIMEOUT_CLOCKS                  64u
#define RESPONSE_RX_WORDS                      2u
#define OBSERVER_RX_WORDS                      1u
#define FIRST_PHASE_COUNT                      6u
#define TABLE_ENTRY_WORDS                      3u
#define MAX_SCAN_DEPTH                        32u
#define HIT_STAGE_COUNT                        5u
#define STAGE_COUNT                            6u
#define RESET_ROM_BASE                   0xFFFF0u
#define V30_ROM_BASE                     0xF0000u
#define EXPECTED_RESET_WORD               0x00EAu
#define OUT_BASE                               0u
#define OUT_COUNT                             28u
#define RESPONSE_VALID_BIT                    28u
#define QUALIFIED_T1_CONTROL_BITS ((1u << V30_PIN_ASTB) | \
                                   (1u << V30_PIN_IOM) | \
                                   (1u << V30_PIN_INTAK))

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} probe_sm_t;

typedef struct {
    uint32_t depth;
    bool expect_hit;
} stage_case_t;

typedef struct {
    uint32_t depth;
    bool expect_hit;
    bool reset_ok;
    bool pre_release_clean;
    bool pio2_clock_only;
    bool pio1_ad_released_before;
    bool first_address_ok;
    bool ordinal_ok;
    bool deadline_gate_ok;
    bool response_phase_ok;
    bool no_drive_authorization;
    bool pio1_ad_released_after;
    bool terminal_safe;
    uint32_t pre_pio1_padoe;
    uint32_t pre_pio2_padoe;
    uint32_t post_pio1_padoe;
    uint32_t tx_fifo_pre;
    uint32_t tx_dma_pre;
    uint32_t rx_dma_pre;
    uint32_t observer_dma_pre;
    uint32_t tx_dma_post;
    uint32_t rx_dma_post;
    uint32_t observer_dma_post;
    uint32_t tx_fifo_post;
    uint32_t response_fifo_residue;
    uint32_t observer_fifo_residue;
    uint32_t response_words;
    uint32_t response_rx[RESPONSE_RX_WORDS];
    uint32_t observer_words;
    uint32_t observer_raw;
    uint32_t phase_raw[FIRST_PHASE_COUNT];
    uint32_t phase_count;
} stage_result_t;

static const stage_case_t stage_cases[STAGE_COUNT] = {
    {1u, true}, {4u, true}, {8u, true}, {16u, true}, {32u, true},
    {32u, false},
};

static uint32_t g_response_table[MAX_SCAN_DEPTH * TABLE_ENTRY_WORDS];
static uint32_t g_response_rx[RESPONSE_RX_WORDS];
static uint32_t g_observer_rx[OBSERVER_RX_WORDS];

static const uint8_t ad_pins[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(sample, ad_pins[bit]) << bit);
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

static uint32_t encode_gpio_word(uint16_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if ((value & (1u << bit)) != 0u)
            encoded |= 1u << ad_pins[bit];
    }
    return encoded;
}

static uint32_t encode_gpio_address(uint32_t address) {
    uint32_t encoded = encode_gpio_word((uint16_t)address);
    if (address & (1u << 16)) encoded |= 1u << V30_PIN_A16;
    if (address & (1u << 17)) encoded |= 1u << V30_PIN_A17;
    if (address & (1u << 18)) encoded |= 1u << V30_PIN_A18;
    if (address & (1u << 19)) encoded |= 1u << V30_PIN_A19;
    return encoded;
}

static uint32_t encoded_drive_command(uint16_t value) {
    return encode_gpio_word(value) | (1u << RESPONSE_VALID_BIT);
}

static uint32_t make_qualified_key(uint32_t address) {
    return QUALIFIED_T1_CONTROL_BITS | encode_gpio_address(address);
}

static uint64_t timeout_us_from_clocks(uint32_t clocks) {
    return ((uint64_t)clocks * 1000000ull + C0C1A2_V30_HZ - 1u) /
           C0C1A2_V30_HZ + 2u;
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

static void route_ad_to_sio_high_z(void) {
    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_pins[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static bool ad_is_sio_high_z(void) {
    if ((sio_hw->gpio_oe & V30_AD_BUS_MASK) != 0u) return false;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (gpio_get_function(ad_pins[bit]) != GPIO_FUNC_SIO) return false;
    }
    return true;
}

static void route_ad_to_response(const probe_sm_t *response) {
    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(response->pio, ad_pins[bit]);
}

static bool ad_is_pio1_released(void) {
    if ((pio1->dbg_padoe & V30_AD_BUS_MASK) != 0u) return false;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (gpio_get_function(ad_pins[bit]) != GPIO_FUNC_PIO1) return false;
    }
    return true;
}

static void clock_init(probe_sm_t *clock) {
    clock->pio = pio2;
    clock->sm = pio_claim_unused_sm(clock->pio, true);
    clock->offset = pio_add_program(clock->pio, &perf_continuous_clk_program);
}

static void clock_prepare(probe_sm_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);

    pio_sm_config c = perf_continuous_clk_program_get_default_config(
        clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)C0C1A2_V30_HZ));
    pio_gpio_init(clock->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm,
                                  V30_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(clock->pio, clock->sm,
                            clock->offset, &c) == PICO_OK);
    pio_sm_set_pins_with_mask(clock->pio, clock->sm,
                              0u, 1u << V30_PIN_CLK);
}

static void clock_start(probe_sm_t *clock) {
    clock_prepare(clock);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
}

static void clock_stop_low(probe_sm_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t edge_timeout = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    for (uint i = 0u; i < count; ++i) {
        uint64_t deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline && !gpio_get(V30_PIN_CLK))
            tight_loop_contents();
        if (!gpio_get(V30_PIN_CLK)) return false;

        deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline && gpio_get(V30_PIN_CLK))
            tight_loop_contents();
        if (gpio_get(V30_PIN_CLK)) return false;
    }
    return true;
}

static void response_init(probe_sm_t *response) {
    response->pio = pio1;
    response->sm = pio_claim_unused_sm(response->pio, true);
    response->offset = pio_add_program(
        response->pio, &pc1c_arbitrary_rom_response_program);
    pio_sm_config c = pc1c_arbitrary_rom_response_program_get_default_config(
        response->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_out_pins(&c, OUT_BASE, OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_jmp_pin(&c, V30_PIN_ASTB);
    hard_assert(pio_sm_init(response->pio, response->sm,
                            response->offset, &c) == PICO_OK);
    pio_sm_set_enabled(response->pio, response->sm, false);
}

static void observer_init(probe_sm_t *observer) {
    observer->pio = pio0;
    observer->sm = pio_claim_unused_sm(observer->pio, true);
    observer->offset = pio_add_program(
        observer->pio, &pc1c_a2_first_address_observer_program);
    pio_sm_config c =
        pc1c_a2_first_address_observer_program_get_default_config(
            observer->offset);
    sm_config_set_in_pins(&c, 0u);
    hard_assert(pio_sm_init(observer->pio, observer->sm,
                            observer->offset, &c) == PICO_OK);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
}

static void phase_capture_init(probe_sm_t *phase) {
    phase->pio = pio0;
    phase->sm = pio_claim_unused_sm(phase->pio, true);
    phase->offset = pio_add_program(
        phase->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(
        phase->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(phase->pio, phase->sm,
                            phase->offset, &c) == PICO_OK);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
}

static void arm_sm(probe_sm_t *sm) {
    pio_sm_set_enabled(sm->pio, sm->sm, false);
    pio_sm_clear_fifos(sm->pio, sm->sm);
    pio_sm_restart(sm->pio, sm->sm);
    pio_sm_exec(sm->pio, sm->sm, pio_encode_jmp(sm->offset));
}

static uint32_t dma_remaining(int channel) {
    return dma_channel_hw_addr((uint)channel)->transfer_count & 0x0FFFFFFFu;
}

static int start_tx_dma(const probe_sm_t *response,
                        const uint32_t *words, uint32_t count) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c,
                            pio_get_dreq(response->pio, response->sm, true));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c,
                          &response->pio->txf[response->sm], words,
                          count, true);
    return channel;
}

static int start_rx_dma(const probe_sm_t *sm, uint32_t *words,
                        uint32_t count) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(sm->pio, sm->sm, false));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c, words,
                          &sm->pio->rxf[sm->sm], count, true);
    return channel;
}

static void stop_dma(int channel) {
    dma_channel_abort((uint)channel);
    dma_channel_unclaim((uint)channel);
}

static bool wait_tx_primed(const probe_sm_t *response, uint32_t level) {
    const uint64_t deadline = time_us_64() + 10000u;
    while (time_us_64() <= deadline) {
        if (pio_sm_get_tx_fifo_level(response->pio, response->sm) >= level)
            return true;
        tight_loop_contents();
    }
    return false;
}

static uint32_t prepare_response_table(const stage_case_t *stage) {
    hard_assert(stage->depth > 0u && stage->depth <= MAX_SCAN_DEPTH);
    for (uint32_t i = 0u; i < stage->depth; ++i) {
        const bool target = stage->expect_hit && i + 1u == stage->depth;
        const uint32_t address = target ? RESET_ROM_BASE :
            V30_ROM_BASE + i * 2u;
        const uint16_t value = target ? EXPECTED_RESET_WORD :
            (uint16_t)(0xA500u | i);
        g_response_table[i * TABLE_ENTRY_WORDS] =
            make_qualified_key(address);
        g_response_table[i * TABLE_ENTRY_WORDS + 1u] =
            encoded_drive_command(value);
        g_response_table[i * TABLE_ENTRY_WORDS + 2u] = i + 1u;
    }
    return stage->depth * TABLE_ENTRY_WORDS;
}

static bool stage_valid(const stage_result_t *result) {
    const uint32_t expected_words = result->expect_hit ? RESPONSE_RX_WORDS : 0u;
    return result->reset_ok && result->pre_release_clean &&
           result->response_words == expected_words &&
           result->observer_words == OBSERVER_RX_WORDS &&
           result->phase_count == FIRST_PHASE_COUNT;
}

static bool stage_pass(const stage_result_t *result) {
    const bool functional = result->expect_hit ?
        (result->ordinal_ok && result->deadline_gate_ok &&
         result->response_phase_ok) : result->no_drive_authorization;
    const uint32_t expected_rx_remain = result->expect_hit ? 0u :
        RESPONSE_RX_WORDS;
    return stage_valid(result) && result->first_address_ok && functional &&
           result->pio2_clock_only && result->pio1_ad_released_before &&
           result->pio1_ad_released_after && result->terminal_safe &&
           result->tx_dma_post == 0u &&
           result->rx_dma_post == expected_rx_remain &&
           result->observer_dma_post == 0u && result->tx_fifo_post == 0u &&
           result->response_fifo_residue == 0u &&
           result->observer_fifo_residue == 0u;
}

static void run_stage(probe_sm_t *clock, probe_sm_t *response,
                      probe_sm_t *observer, probe_sm_t *phase,
                      const stage_case_t *stage, stage_result_t *result) {
    *result = (stage_result_t){0};
    result->depth = stage->depth;
    result->expect_hit = stage->expect_hit;
    const uint32_t table_words = prepare_response_table(stage);
    for (uint i = 0u; i < RESPONSE_RX_WORDS; ++i) g_response_rx[i] = 0u;
    g_observer_rx[0] = 0u;

    gpio_put(V30_PIN_INTR, false);
    gpio_put(V30_PIN_RESET, true);
    route_ad_to_sio_high_z();

    clock_start(clock);
    result->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    clock_stop_low(clock);

    clock_prepare(clock);
    arm_sm(response);
    pio_sm_exec(response->pio, response->sm,
                pio_encode_mov(pio_pindirs, pio_null));
    arm_sm(observer);
    arm_sm(phase);
    route_ad_to_response(response);

    const int tx_dma = start_tx_dma(response, g_response_table, table_words);
    const int rx_dma = start_rx_dma(response, g_response_rx,
                                    RESPONSE_RX_WORDS);
    const int observer_dma = start_rx_dma(observer, g_observer_rx,
                                          OBSERVER_RX_WORDS);
    const uint32_t prime_level = table_words < 4u ? table_words : 4u;
    const bool tx_primed = wait_tx_primed(response, prime_level);

    result->tx_fifo_pre = pio_sm_get_tx_fifo_level(response->pio,
                                                    response->sm);
    result->tx_dma_pre = dma_remaining(tx_dma);
    result->rx_dma_pre = dma_remaining(rx_dma);
    result->observer_dma_pre = dma_remaining(observer_dma);
    result->pre_pio1_padoe = pio1->dbg_padoe;
    result->pre_pio2_padoe = pio2->dbg_padoe;
    result->pio1_ad_released_before = ad_is_pio1_released();
    result->pio2_clock_only =
        result->pre_pio2_padoe == (1u << V30_PIN_CLK);
    result->pre_release_clean = tx_primed &&
        result->tx_fifo_pre == prime_level &&
        result->rx_dma_pre == RESPONSE_RX_WORDS &&
        result->observer_dma_pre == OBSERVER_RX_WORDS &&
        pio_sm_is_rx_fifo_empty(response->pio, response->sm) &&
        pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
        pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
        result->pio1_ad_released_before && result->pio2_clock_only &&
        !gpio_get(V30_PIN_CLK);

    pio_enable_sm_mask_in_sync(pio0,
        (1u << observer->sm) | (1u << phase->sm));

    if (result->reset_ok && result->pre_release_clean) {
        const uint32_t irq_state = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_sm_set_enabled(response->pio, response->sm, true);
        pio_sm_set_enabled(clock->pio, clock->sm, true);
        const uint64_t deadline =
            time_us_64() + timeout_us_from_clocks(STAGE_TIMEOUT_CLOCKS);
        while (pio_sm_get_rx_fifo_level(phase->pio, phase->sm) <
                   FIRST_PHASE_COUNT &&
               time_us_64() <= deadline)
            tight_loop_contents();
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
        restore_interrupts(irq_state);
    } else {
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
    }

    result->tx_dma_post = dma_remaining(tx_dma);
    result->rx_dma_post = dma_remaining(rx_dma);
    result->observer_dma_post = dma_remaining(observer_dma);
    result->tx_fifo_post = pio_sm_get_tx_fifo_level(response->pio,
                                                     response->sm);
    result->post_pio1_padoe = pio1->dbg_padoe;
    result->pio1_ad_released_after =
        (result->post_pio1_padoe & V30_AD_BUS_MASK) == 0u;

    pio_sm_set_enabled(response->pio, response->sm, false);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
    pio_sm_exec(response->pio, response->sm,
                pio_encode_mov(pio_pindirs, pio_null));

    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           result->phase_count < FIRST_PHASE_COUNT) {
        result->phase_raw[result->phase_count++] =
            pio_sm_get(phase->pio, phase->sm);
    }

    result->response_fifo_residue =
        pio_sm_get_rx_fifo_level(response->pio, response->sm);
    result->observer_fifo_residue =
        pio_sm_get_rx_fifo_level(observer->pio, observer->sm);
    result->response_words = RESPONSE_RX_WORDS - result->rx_dma_post;
    result->observer_words = OBSERVER_RX_WORDS - result->observer_dma_post;
    for (uint i = 0u; i < result->response_words; ++i)
        result->response_rx[i] = g_response_rx[i];
    if (result->observer_words != 0u) result->observer_raw = g_observer_rx[0];

    stop_dma(tx_dma);
    stop_dma(rx_dma);
    stop_dma(observer_dma);
    route_ad_to_sio_high_z();
    gpio_put(V30_PIN_INTR, false);

    result->first_address_ok = result->observer_words == 1u &&
        decode_address(result->observer_raw) == RESET_ROM_BASE;
    result->ordinal_ok = result->expect_hit && result->response_words >= 1u &&
        result->response_rx[0] == result->depth;
    result->deadline_gate_ok = result->expect_hit &&
        result->response_words >= 2u &&
        sample_bit(result->response_rx[1], V30_PIN_ASTB) != 0u;
    result->response_phase_ok = result->expect_hit &&
        result->phase_count == FIRST_PHASE_COUNT &&
        decode_ad(result->phase_raw[3]) == EXPECTED_RESET_WORD &&
        decode_ad(result->phase_raw[4]) == EXPECTED_RESET_WORD &&
        decode_ad(result->phase_raw[5]) == EXPECTED_RESET_WORD;
    result->no_drive_authorization = !result->expect_hit &&
        result->response_words == 0u;
    result->terminal_safe = gpio_get(V30_PIN_RESET) &&
                            !gpio_get(V30_PIN_CLK) && ad_is_sio_high_z();
}

static const char *pass_fail(bool pass) {
    return pass ? "PASS" : "FAIL";
}

static void print_phase(const stage_result_t *result) {
    static const char *const names[FIRST_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    for (uint i = 0u; i < result->phase_count; ++i) {
        printf("  %-2s raw=%08lX ASTB=%u CLK=%u AD=%04X\n",
               names[i], (unsigned long)result->phase_raw[i],
               (unsigned)sample_bit(result->phase_raw[i], V30_PIN_ASTB),
               (unsigned)sample_bit(result->phase_raw[i], V30_PIN_CLK),
               decode_ad(result->phase_raw[i]));
    }
}

static void print_results(const stage_result_t results[STAGE_COUNT]) {
    bool overall = true;
    uint32_t deepest_response = 0u;
    printf("\nPC1-C0C1-A2 Same-Cycle Arbitrary Selector Response - 0.300 MHz\n");
    printf("Realtime path       : early-T1 key -> PIO1 scan -> AD/PINDIRS\n");
    printf("Table transport     : internal SRAM -> DMA -> PIO1 TX FIFO\n");
    printf("Current-cycle M33   : NONE\n");
    printf("Clock owner         : PIO2; response owner: PIO1\n");
    printf("Observer path       : passive PIO0 address + phase snapshots\n");
    printf("Input synchronizers : SDK defaults\n");
    printf("Late/miss policy    : no PINDIRS authorization; AD remains high-Z\n\n");

    printf("[SUMMARY]\n");
    printf("case depth address gate R2/F2/R3 no-drive result\n");
    for (uint i = 0u; i < STAGE_COUNT; ++i) {
        const stage_result_t *result = &results[i];
        const bool pass = stage_pass(result);
        if (result->expect_hit && pass) deepest_response = result->depth;
        if (!pass) overall = false;
        printf("%-4s %5lu %-7s %-4s %-8s %-8s %s\n",
               result->expect_hit ? "HIT" : "MISS",
               (unsigned long)result->depth,
               pass_fail(result->first_address_ok),
               result->expect_hit ? pass_fail(result->deadline_gate_ok) : "N/A",
               result->expect_hit ? pass_fail(result->response_phase_ok) : "N/A",
               result->expect_hit ? "N/A" :
                   pass_fail(result->no_drive_authorization),
               pass_fail(pass));
    }
    printf("Deepest same-cycle response = %lu entries\n",
           (unsigned long)deepest_response);
    printf("Explicit depth-32 miss      = %s\n",
           pass_fail(stage_pass(&results[STAGE_COUNT - 1u])));
    printf("C0C1-A2 RESULT              = %s\n", pass_fail(overall));

    printf("\n[ENGINEERING DETAILS]\n");
    for (uint i = 0u; i < STAGE_COUNT; ++i) {
        const stage_result_t *result = &results[i];
        printf("\n-- %s depth %lu --\n",
               result->expect_hit ? "hit" : "miss",
               (unsigned long)result->depth);
        printf("RESET / clean epoch       = %s / %s\n",
               pass_fail(result->reset_ok),
               pass_fail(result->pre_release_clean));
        printf("PIO2 CLK OE / PIO1 pre-OE = %08lX / %08lX %s\n",
               (unsigned long)result->pre_pio2_padoe,
               (unsigned long)result->pre_pio1_padoe,
               pass_fail(result->pio2_clock_only &&
                         result->pio1_ad_released_before));
        printf("Passive observer address  = %05lX %s\n",
               (unsigned long)decode_address(result->observer_raw),
               pass_fail(result->first_address_ok));
        printf("Response RX words          = %lu/%u\n",
               (unsigned long)result->response_words,
               result->expect_hit ? RESPONSE_RX_WORDS : 0u);
        if (result->response_words >= 1u)
            printf("Selected ordinal           = %lu/%lu %s\n",
                   (unsigned long)result->response_rx[0],
                   (unsigned long)result->depth,
                   pass_fail(result->ordinal_ok));
        if (result->response_words >= 2u)
            printf("Deadline raw               = %08lX ASTB=%u %s\n",
                   (unsigned long)result->response_rx[1],
                   (unsigned)sample_bit(result->response_rx[1],
                                        V30_PIN_ASTB),
                   pass_fail(result->deadline_gate_ok));
        printf("R2/F2/R3 response          = %04X/%04X/%04X %s\n",
               result->phase_count > 3u ? decode_ad(result->phase_raw[3]) : 0u,
               result->phase_count > 4u ? decode_ad(result->phase_raw[4]) : 0u,
               result->phase_count > 5u ? decode_ad(result->phase_raw[5]) : 0u,
               result->expect_hit ? pass_fail(result->response_phase_ok) :
                   "NOT AUTHORIZED");
        printf("DMA remain TX/RX/observer = %lu/%lu/%lu\n",
               (unsigned long)result->tx_dma_post,
               (unsigned long)result->rx_dma_post,
               (unsigned long)result->observer_dma_post);
        printf("FIFO remain TX/RX/observer= %lu/%lu/%lu\n",
               (unsigned long)result->tx_fifo_post,
               (unsigned long)result->response_fifo_residue,
               (unsigned long)result->observer_fifo_residue);
        printf("PIO1 post-release OE       = %08lX %s\n",
               (unsigned long)result->post_pio1_padoe,
               pass_fail(result->pio1_ad_released_after));
        printf("First-cycle phase words    = %lu/6\n",
               (unsigned long)result->phase_count);
        print_phase(result);
        printf("Terminal safe              = %s\n",
               pass_fail(result->terminal_safe));
        printf("Stage result               = %s\n",
               pass_fail(stage_pass(result)));
    }

    printf("\nInterpretation: PASS proves bounded current-address selection and\n");
    printf("same-cycle first-word response only; general C0C1 ROM remains open.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    prepare_header_high_z();
    init_control_outputs();
    route_ad_to_sio_high_z();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    probe_sm_t clock, response, observer, phase;
    clock_init(&clock);
    response_init(&response);
    observer_init(&observer);
    phase_capture_init(&phase);

    static stage_result_t results[STAGE_COUNT];
    for (uint i = 0u; i < STAGE_COUNT; ++i)
        run_stage(&clock, &response, &observer, &phase,
                  &stage_cases[i], &results[i]);

    print_results(results);
    fflush(stdout);
    while (true) tight_loop_contents();
}
