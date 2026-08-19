/*
 * PC1-C0C1-A non-driving current-address selector feasibility probe.
 *
 * Each stage creates a new clock-only RESET qualification and clean epoch.
 * PIO1 captures the first physical early-T1 key, then scans a DMA-fed SRAM
 * table of {raw key, candidate value, ordinal} entries. On a match it returns
 * the selected value and a GPIO phase marker to SRAM. It never executes OUT,
 * SET, or PINDIRS and never owns AD0-AD15.
 *
 * The target entry is deliberately placed at scan depths 1/4/8/16/32. A stage
 * passes only if the current physical FFFF0 key selects 00EA and the completion
 * marker still has ASTB high. This measures whether a bounded table selector
 * can finish before the accepted responder's ASTB-fall drive boundary.
 *
 * This is a feasibility instrument, not arbitrary-address ROM service. It does
 * not solve table rewind/refill between general bus cycles and cannot satisfy
 * PC1-C0C1 by itself.
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
#include "pc1c_arbitrary_rom_feasibility.pio.h"
#include "perf_continuous_clock.pio.h"
#include "v30/v30_pins.h"

#define C0C1A_V30_HZ                    300000u
#define RESET_CLOCKS                        20u
#define SIGNAL_TIMEOUT_CLOCKS               64u
#define STAGE_TIMEOUT_CLOCKS                64u
#define PROBE_RX_WORDS                       4u
#define OBSERVER_RX_WORDS                    1u
#define FIRST_PHASE_COUNT                    6u
#define TABLE_ENTRY_WORDS                    3u
#define MAX_SCAN_DEPTH                      32u
#define STAGE_COUNT                          5u
#define RESET_ROM_BASE                 0xFFFF0u
#define V30_ROM_BASE                   0xF0000u
#define EXPECTED_RESET_WORD             0x00EAu
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
    bool reset_ok;
    bool pre_release_clean;
    bool clock_only_oe;
    bool first_address_ok;
    bool selector_key_ok;
    bool selected_value_ok;
    bool selected_ordinal_ok;
    bool completed_before_astb_fall;
    bool ad_passive;
    bool terminal_safe;
    uint32_t pre_pio1_padoe;
    uint32_t tx_fifo_pre;
    uint32_t tx_dma_pre;
    uint32_t rx_dma_pre;
    uint32_t observer_dma_pre;
    uint32_t tx_dma_post;
    uint32_t rx_dma_post;
    uint32_t observer_dma_post;
    uint32_t tx_fifo_post;
    uint32_t selector_fifo_residue;
    uint32_t observer_fifo_residue;
    uint32_t selector_words;
    uint32_t observer_words;
    uint32_t selector_raw[PROBE_RX_WORDS];
    uint32_t observer_raw;
    uint32_t phase_raw[FIRST_PHASE_COUNT];
    uint32_t phase_count;
} stage_result_t;

static const uint32_t scan_depths[STAGE_COUNT] = {1u, 4u, 8u, 16u, 32u};
static uint32_t g_selector_table[MAX_SCAN_DEPTH * TABLE_ENTRY_WORDS];
static uint32_t g_selector_rx[PROBE_RX_WORDS];
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

static uint64_t timeout_us_from_clocks(uint32_t clocks) {
    return ((uint64_t)clocks * 1000000ull + C0C1A_V30_HZ - 1u) /
           C0C1A_V30_HZ + 2u;
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

static void release_ad(void) {
    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_pins[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static bool ad_is_passive(void) {
    if ((sio_hw->gpio_oe & V30_AD_BUS_MASK) != 0u) return false;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (gpio_get_function(ad_pins[bit]) != GPIO_FUNC_SIO) return false;
    }
    return true;
}

static void clock_init(probe_sm_t *clock) {
    clock->pio = pio1;
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
        (float)clock_get_hz(clk_sys) / (2.0f * (float)C0C1A_V30_HZ));
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

static void selector_init(probe_sm_t *selector) {
    selector->pio = pio1;
    selector->sm = pio_claim_unused_sm(selector->pio, true);
    selector->offset = pio_add_program(
        selector->pio, &pc1c_arbitrary_selector_probe_program);
    pio_sm_config c = pc1c_arbitrary_selector_probe_program_get_default_config(
        selector->offset);
    sm_config_set_in_pins(&c, 0u);
    hard_assert(pio_sm_init(selector->pio, selector->sm,
                            selector->offset, &c) == PICO_OK);
    pio_sm_set_enabled(selector->pio, selector->sm, false);
}

static void observer_init(probe_sm_t *observer) {
    observer->pio = pio0;
    observer->sm = pio_claim_unused_sm(observer->pio, true);
    observer->offset = pio_add_program(
        observer->pio, &pc1c_c0c1_first_address_observer_program);
    pio_sm_config c =
        pc1c_c0c1_first_address_observer_program_get_default_config(
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

static int start_tx_dma(const probe_sm_t *selector,
                        const uint32_t *words, uint32_t count) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c,
                            pio_get_dreq(selector->pio, selector->sm, true));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c,
                          &selector->pio->txf[selector->sm], words,
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

static bool wait_tx_primed(const probe_sm_t *selector, uint32_t level) {
    const uint64_t deadline = time_us_64() + 10000u;
    while (time_us_64() <= deadline) {
        if (pio_sm_get_tx_fifo_level(selector->pio, selector->sm) >= level)
            return true;
        tight_loop_contents();
    }
    return false;
}

static uint32_t make_qualified_key(uint32_t address) {
    return QUALIFIED_T1_CONTROL_BITS | encode_gpio_address(address);
}

static uint32_t prepare_selector_table(uint32_t depth) {
    hard_assert(depth > 0u && depth <= MAX_SCAN_DEPTH);
    for (uint32_t i = 0u; i < depth; ++i) {
        const bool target = i + 1u == depth;
        const uint32_t address = target ? RESET_ROM_BASE :
            V30_ROM_BASE + i * 2u;
        g_selector_table[i * TABLE_ENTRY_WORDS] = make_qualified_key(address);
        g_selector_table[i * TABLE_ENTRY_WORDS + 1u] = target ?
            EXPECTED_RESET_WORD : (0xA500u | i);
        g_selector_table[i * TABLE_ENTRY_WORDS + 2u] = i + 1u;
    }
    return depth * TABLE_ENTRY_WORDS;
}

static bool stage_valid(const stage_result_t *result) {
    return result->reset_ok && result->pre_release_clean &&
           result->selector_words == PROBE_RX_WORDS &&
           result->observer_words == OBSERVER_RX_WORDS &&
           result->phase_count == FIRST_PHASE_COUNT;
}

static bool stage_pass(const stage_result_t *result) {
    return stage_valid(result) && result->first_address_ok &&
           result->selector_key_ok && result->selected_value_ok &&
           result->selected_ordinal_ok &&
           result->completed_before_astb_fall && result->ad_passive &&
           result->terminal_safe && result->tx_dma_post == 0u &&
           result->rx_dma_post == 0u && result->observer_dma_post == 0u &&
           result->tx_fifo_post == 0u &&
           result->selector_fifo_residue == 0u &&
           result->observer_fifo_residue == 0u;
}

static void run_stage(probe_sm_t *clock, probe_sm_t *selector,
                      probe_sm_t *observer, probe_sm_t *phase,
                      uint32_t depth, stage_result_t *result) {
    *result = (stage_result_t){0};
    result->depth = depth;
    const uint32_t table_words = prepare_selector_table(depth);
    const uint32_t target_key = make_qualified_key(RESET_ROM_BASE);
    for (uint i = 0u; i < PROBE_RX_WORDS; ++i) g_selector_rx[i] = 0u;
    g_observer_rx[0] = 0u;

    gpio_put(V30_PIN_INTR, false);
    gpio_put(V30_PIN_RESET, true);
    release_ad();

    clock_start(clock);
    result->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    clock_stop_low(clock);

    clock_prepare(clock);
    arm_sm(selector);
    arm_sm(observer);
    arm_sm(phase);

    const int tx_dma = start_tx_dma(selector, g_selector_table, table_words);
    const int rx_dma = start_rx_dma(selector, g_selector_rx, PROBE_RX_WORDS);
    const int observer_dma = start_rx_dma(observer, g_observer_rx,
                                          OBSERVER_RX_WORDS);
    const uint32_t prime_level = table_words < 4u ? table_words : 4u;
    const bool tx_primed = wait_tx_primed(selector, prime_level);

    result->tx_fifo_pre = pio_sm_get_tx_fifo_level(selector->pio,
                                                    selector->sm);
    result->tx_dma_pre = dma_remaining(tx_dma);
    result->rx_dma_pre = dma_remaining(rx_dma);
    result->observer_dma_pre = dma_remaining(observer_dma);
    result->pre_pio1_padoe = pio1->dbg_padoe;
    result->clock_only_oe =
        (result->pre_pio1_padoe & (1u << V30_PIN_CLK)) != 0u &&
        (result->pre_pio1_padoe & V30_AD_BUS_MASK) == 0u;
    result->pre_release_clean = tx_primed &&
        result->tx_fifo_pre == prime_level &&
        result->rx_dma_pre == PROBE_RX_WORDS &&
        result->observer_dma_pre == OBSERVER_RX_WORDS &&
        pio_sm_is_rx_fifo_empty(selector->pio, selector->sm) &&
        pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
        pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
        result->clock_only_oe && !gpio_get(V30_PIN_CLK) && ad_is_passive();

    pio_enable_sm_mask_in_sync(pio0,
        (1u << observer->sm) | (1u << phase->sm));

    if (result->reset_ok && result->pre_release_clean) {
        const uint32_t irq_state = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_enable_sm_mask_in_sync(pio1,
            (1u << clock->sm) | (1u << selector->sm));
        const uint64_t deadline =
            time_us_64() + timeout_us_from_clocks(STAGE_TIMEOUT_CLOCKS);
        while (dma_remaining(rx_dma) != 0u && time_us_64() <= deadline)
            tight_loop_contents();

        /*
         * Selection intentionally completes during T1, before the passive
         * AF/R1/F1/R2/F2/R3 instrument can collect its six snapshots. Keep
         * the qualified clock running until that independent witness is
         * complete. Stopping as soon as selector RX DMA finished produced a
         * false FAIL whose phase count depended on M33 scheduling latency.
         */
        while (dma_remaining(rx_dma) == 0u &&
               pio_sm_get_rx_fifo_level(phase->pio, phase->sm) <
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
    result->tx_fifo_post = pio_sm_get_tx_fifo_level(selector->pio,
                                                     selector->sm);

    pio_sm_set_enabled(selector->pio, selector->sm, false);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
    pio_sm_set_enabled(phase->pio, phase->sm, false);

    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           result->phase_count < FIRST_PHASE_COUNT) {
        result->phase_raw[result->phase_count++] =
            pio_sm_get(phase->pio, phase->sm);
    }

    result->selector_fifo_residue =
        pio_sm_get_rx_fifo_level(selector->pio, selector->sm);
    result->observer_fifo_residue =
        pio_sm_get_rx_fifo_level(observer->pio, observer->sm);
    result->selector_words = PROBE_RX_WORDS - result->rx_dma_post;
    result->observer_words = OBSERVER_RX_WORDS - result->observer_dma_post;
    for (uint i = 0u; i < result->selector_words; ++i)
        result->selector_raw[i] = g_selector_rx[i];
    if (result->observer_words != 0u) result->observer_raw = g_observer_rx[0];

    stop_dma(tx_dma);
    stop_dma(rx_dma);
    stop_dma(observer_dma);
    release_ad();
    gpio_put(V30_PIN_INTR, false);

    result->first_address_ok = result->observer_words == 1u &&
        decode_address(result->observer_raw) == RESET_ROM_BASE;
    result->selector_key_ok = result->selector_words >= 1u &&
        result->selector_raw[0] == target_key;
    result->completed_before_astb_fall = result->selector_words >= 2u &&
        sample_bit(result->selector_raw[1], V30_PIN_ASTB) != 0u;
    result->selected_value_ok = result->selector_words >= 3u &&
        result->selector_raw[2] == EXPECTED_RESET_WORD;
    result->selected_ordinal_ok = result->selector_words >= 4u &&
        result->selector_raw[3] == depth;
    result->ad_passive = ad_is_passive();
    result->terminal_safe = gpio_get(V30_PIN_RESET) &&
                            !gpio_get(V30_PIN_CLK) && result->ad_passive;
}

static const char *pass_fail(bool pass) {
    return pass ? "PASS" : "FAIL";
}

static void print_results(const stage_result_t results[STAGE_COUNT]) {
    bool overall = true;
    uint32_t deepest_pass = 0u;
    printf("\nPC1-C0C1-A Non-Driving SRAM Selector Feasibility - 0.300 MHz\n");
    printf("Selector path       : current early-T1 raw key -> PIO1 scan\n");
    printf("Table transport     : internal SRAM -> DMA -> PIO1 TX FIFO\n");
    printf("Selected value      : PIO1 RX FIFO -> DMA -> internal SRAM\n");
    printf("AD bus ownership    : passive SIO input for every stage\n");
    printf("Input synchronizers : SDK defaults\n");
    printf("Drive policy        : NONE; selected 00EA is never placed on AD\n\n");

    printf("[SUMMARY]\n");
    printf("depth address value ordinal before_ASTB_fall result\n");
    for (uint i = 0u; i < STAGE_COUNT; ++i) {
        const stage_result_t *result = &results[i];
        const bool pass = stage_pass(result);
        if (pass) deepest_pass = result->depth;
        else overall = false;
        printf("%5lu %-7s %-5s %-7s %-16s %s\n",
               (unsigned long)result->depth,
               pass_fail(result->selector_key_ok),
               pass_fail(result->selected_value_ok),
               pass_fail(result->selected_ordinal_ok),
               pass_fail(result->completed_before_astb_fall),
               pass_fail(pass));
    }
    printf("Deepest contiguous scan = %lu entries\n",
           (unsigned long)deepest_pass);
    printf("C0C1-A RESULT          = %s\n", pass_fail(overall));

    printf("\n[ENGINEERING DETAILS]\n");
    for (uint i = 0u; i < STAGE_COUNT; ++i) {
        const stage_result_t *result = &results[i];
        printf("\n-- scan depth %lu --\n", (unsigned long)result->depth);
        printf("RESET / clean epoch      = %s / %s\n",
               pass_fail(result->reset_ok),
               pass_fail(result->pre_release_clean));
        printf("PIO1 pre-release OE      = %08lX %s\n",
               (unsigned long)result->pre_pio1_padoe,
               pass_fail(result->clock_only_oe));
        printf("Passive observer address = %05lX %s\n",
               (unsigned long)decode_address(result->observer_raw),
               pass_fail(result->first_address_ok));
        printf("Selector capture raw     = %08lX %s\n",
               (unsigned long)result->selector_raw[0],
               pass_fail(result->selector_key_ok));
        printf("Completion raw           = %08lX ASTB=%u CLK=%u %s\n",
               (unsigned long)result->selector_raw[1],
               (unsigned)sample_bit(result->selector_raw[1], V30_PIN_ASTB),
               (unsigned)sample_bit(result->selector_raw[1], V30_PIN_CLK),
               pass_fail(result->completed_before_astb_fall));
        printf("Selected value / ordinal = %04lX / %lu %s\n",
               (unsigned long)result->selector_raw[2],
               (unsigned long)result->selector_raw[3],
               pass_fail(result->selected_value_ok &&
                         result->selected_ordinal_ok));
        printf("DMA remain TX/RX/observer= %lu/%lu/%lu\n",
               (unsigned long)result->tx_dma_post,
               (unsigned long)result->rx_dma_post,
               (unsigned long)result->observer_dma_post);
        printf("FIFO remain TX/RX/observer= %lu/%lu/%lu\n",
               (unsigned long)result->tx_fifo_post,
               (unsigned long)result->selector_fifo_residue,
               (unsigned long)result->observer_fifo_residue);
        printf("First-cycle phase words  = %lu/6\n",
               (unsigned long)result->phase_count);
        printf("Terminal safe/passive    = %s / %s\n",
               pass_fail(result->terminal_safe),
               pass_fail(result->ad_passive));
        printf("Stage result             = %s\n",
               pass_fail(stage_pass(result)));
    }

    printf("\nInterpretation: PASS proves bounded selector completion only.\n");
    printf("No ROM response was driven; PC1-C0C1 remains open.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    prepare_header_high_z();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    probe_sm_t clock, selector, observer, phase;
    clock_init(&clock);
    selector_init(&selector);
    observer_init(&observer);
    phase_capture_init(&phase);

    static stage_result_t results[STAGE_COUNT];
    for (uint i = 0u; i < STAGE_COUNT; ++i)
        run_stage(&clock, &selector, &observer, &phase,
                  scan_depths[i], &results[i]);

    print_results(results);
    fflush(stdout);
    while (true) tight_loop_contents();
}
