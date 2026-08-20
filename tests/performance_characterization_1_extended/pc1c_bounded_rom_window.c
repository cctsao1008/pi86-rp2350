/*
 * PC1-C0C1-B1 bounded multi-cycle current-address ROM execution.
 *
 * One immutable 32-entry table describes the complete accepted hit set. The
 * table is repeated in SRAM only to give DMA a finite execution budget; no
 * copy predicts the V30 fetch order. PIO1 restarts at entry zero for every
 * ASTB cycle, selects from the current early-T1 raw key, and treats the table
 * sentinel as an explicit high-Z miss. PIO2 owns CLK. PIO0 independently
 * records address/control and R2 data pairs plus first-cycle phase evidence.
 *
 * This target intentionally reuses the small Native BIOS foundation image.
 * A physical PASS upgrades it from descriptor-order execution to bounded
 * current-address execution. It does not yet provide RAM, INT, or CDC input.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "native_bios_rom.h"
#include "pc1b_first_cycle_phase_capture.pio.h"
#include "pc1c_bounded_rom_window.pio.h"
#include "pc1c_sram_rom_execution_observer.pio.h"
#include "perf_continuous_clock.pio.h"
#include "v30/v30_pins.h"

#define V30_HZ                         300000u
#define RESET_CLOCKS                       20u
#define RUN_TIMEOUT_CLOCKS               4096u
#define TABLE_ENTRIES                      32u
#define ENTRY_WORDS                         3u
#define BLOCK_WORDS (TABLE_ENTRIES * ENTRY_WORDS + 1u)
#define EXECUTION_BUDGET_CYCLES            80u
#define OBSERVER_CYCLES                    64u
#define OBSERVER_WORDS (OBSERVER_CYCLES * 2u)
#define FIRST_PHASE_COUNT                   6u
#define RESET_ROM_BASE                0xFFFF0u
#define BIOS_BASE                     0xF0000u
#define DIAGNOSTIC_PORT                 0x00E9u
#define OUT_COUNT                          28u
#define QUALIFIED_T1_CONTROL_BITS ((1u << V30_PIN_ASTB) | \
                                   (1u << V30_PIN_IOM) | \
                                   (1u << V30_PIN_INTAK))

typedef struct { PIO pio; uint sm; uint offset; } engine_sm_t;

typedef struct {
    bool reset_ok;
    bool clean_epoch;
    bool first_address_ok;
    bool first_response_ok;
    bool far_target_seen;
    bool diagnostic_ok;
    bool checkpoint_ok;
    bool supported_reads_ok;
    bool terminal_safe;
    uint32_t pre_pio1_oe;
    uint32_t pre_pio2_oe;
    uint32_t post_pio1_oe;
    uint32_t tx_dma_pre;
    uint32_t tx_dma_post;
    uint32_t observer_words;
    uint32_t complete_cycles;
    uint32_t supported_reads;
    uint32_t unsupported_cycles;
    uint32_t checkpoint_reads;
    uint32_t phase_count;
    uint32_t phase_raw[FIRST_PHASE_COUNT];
    char diagnostic[32];
} result_t;

static uint32_t g_table_stream[EXECUTION_BUDGET_CYCLES * BLOCK_WORDS];
static uint32_t g_observer[OBSERVER_WORDS];

static const uint8_t ad_pins[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static inline uint32_t bit(uint32_t raw, uint gpio) {
    return (raw >> gpio) & 1u;
}

static uint16_t decode_ad(uint32_t raw) {
    uint16_t value = 0u;
    for (uint i = 0u; i < 16u; ++i)
        value |= (uint16_t)(bit(raw, ad_pins[i]) << i);
    return value;
}

static uint32_t decode_address(uint32_t raw) {
    uint32_t address = decode_ad(raw);
    address |= bit(raw, V30_PIN_A16) << 16;
    address |= bit(raw, V30_PIN_A17) << 17;
    address |= bit(raw, V30_PIN_A18) << 18;
    address |= bit(raw, V30_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static bool memory_read(uint32_t raw) {
    return bit(raw, V30_PIN_IOM) && !bit(raw, V30_PIN_BUFRW) &&
           bit(raw, V30_PIN_INTAK);
}

static bool io_write(uint32_t raw) {
    return !bit(raw, V30_PIN_IOM) && bit(raw, V30_PIN_BUFRW) &&
           bit(raw, V30_PIN_INTAK);
}

static uint32_t encode_word(uint16_t value) {
    uint32_t raw = 0u;
    for (uint i = 0u; i < 16u; ++i)
        if (value & (1u << i)) raw |= 1u << ad_pins[i];
    return raw;
}

static uint32_t encode_address(uint32_t address) {
    uint32_t raw = encode_word((uint16_t)address);
    if (address & 0x10000u) raw |= 1u << V30_PIN_A16;
    if (address & 0x20000u) raw |= 1u << V30_PIN_A17;
    if (address & 0x40000u) raw |= 1u << V30_PIN_A18;
    if (address & 0x80000u) raw |= 1u << V30_PIN_A19;
    return raw;
}

static uint32_t qualified_key(uint32_t address) {
    return QUALIFIED_T1_CONTROL_BITS | encode_address(address);
}

static bool rom_word(uint32_t address, uint16_t *value) {
    static const uint8_t reset_vector[6] = {0xEA, 0x00, 0x00, 0x00, 0xF0, 0x90};
    if ((address & 1u) != 0u) return false;
    if (address >= RESET_ROM_BASE && address < RESET_ROM_BASE + sizeof reset_vector) {
        const uint32_t i = address - RESET_ROM_BASE;
        *value = (uint16_t)reset_vector[i] | ((uint16_t)reset_vector[i + 1u] << 8);
        return true;
    }
    if (address >= BIOS_BASE && address + 1u < BIOS_BASE + native_bios_rom_size) {
        const uint32_t i = address - BIOS_BASE;
        *value = (uint16_t)native_bios_rom_data[i] |
                 ((uint16_t)native_bios_rom_data[i + 1u] << 8);
        return true;
    }
    return false;
}

static void append_entry(uint32_t *block, uint32_t slot,
                         uint32_t address, uint16_t value) {
    block[slot * ENTRY_WORDS] = 0u;
    block[slot * ENTRY_WORDS + 1u] = qualified_key(address);
    block[slot * ENTRY_WORDS + 2u] = encode_word(value);
}

static void prepare_table_stream(void) {
    const uint32_t image_words = (native_bios_rom_size + 1u) / 2u;
    hard_assert(native_bios_rom_size >= 2u);
    hard_assert(image_words + 3u <= TABLE_ENTRIES);

    uint32_t prototype[BLOCK_WORDS];
    uint32_t slot = 0u;
    const uint32_t dummy_count = TABLE_ENTRIES - image_words - 3u;
    for (uint32_t i = 0u; i < dummy_count; ++i)
        append_entry(prototype, slot++, 0xE0000u + i * 2u,
                     (uint16_t)(0xD000u | i));
    append_entry(prototype, slot++, RESET_ROM_BASE + 2u, 0x0000u);
    append_entry(prototype, slot++, RESET_ROM_BASE + 4u, 0x90F0u);
    for (uint32_t i = 0u; i < image_words; ++i) {
        const uint32_t address = BIOS_BASE + i * 2u;
        uint16_t value = 0x90u;
        hard_assert(rom_word(address, &value));
        append_entry(prototype, slot++, address, value);
    }
    append_entry(prototype, slot++, RESET_ROM_BASE, 0x00EAu);
    hard_assert(slot == TABLE_ENTRIES);
    prototype[TABLE_ENTRIES * ENTRY_WORDS] = 1u;

    for (uint32_t cycle = 0u; cycle < EXECUTION_BUDGET_CYCLES; ++cycle)
        memcpy(&g_table_stream[cycle * BLOCK_WORDS], prototype,
               sizeof prototype);
}

static uint64_t timeout_us(uint32_t clocks) {
    return ((uint64_t)clocks * 1000000ull + V30_HZ - 1u) / V30_HZ + 2u;
}

static void header_high_z(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio); gpio_set_dir(gpio, GPIO_IN); gpio_disable_pulls(gpio);
    }
}

static void controls_init(void) {
    gpio_init(V30_PIN_RESET); gpio_put(V30_PIN_RESET, true);
    gpio_set_dir(V30_PIN_RESET, GPIO_OUT); gpio_disable_pulls(V30_PIN_RESET);
    gpio_init(V30_PIN_INTR); gpio_put(V30_PIN_INTR, false);
    gpio_set_dir(V30_PIN_INTR, GPIO_OUT); gpio_disable_pulls(V30_PIN_INTR);
}

static void ad_to_sio(void) {
    for (uint i = 0u; i < 16u; ++i) gpio_set_function(ad_pins[i], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static bool safe_terminal(void) {
    if (!gpio_get(V30_PIN_RESET) || gpio_get(V30_PIN_CLK) ||
        (sio_hw->gpio_oe & V30_AD_BUS_MASK)) return false;
    for (uint i = 0u; i < 16u; ++i)
        if (gpio_get_function(ad_pins[i]) != GPIO_FUNC_SIO) return false;
    return true;
}

static void clock_init(engine_sm_t *s) {
    s->pio = pio2; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_continuous_clk_program);
}

static void clock_prepare(engine_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    gpio_init(V30_PIN_CLK); gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT); gpio_disable_pulls(V30_PIN_CLK);
    pio_sm_config c = perf_continuous_clk_program_get_default_config(s->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / (2.0f * V30_HZ));
    pio_gpio_init(s->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(s->pio, s->sm, V30_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
    pio_sm_set_pins_with_mask(s->pio, s->sm, 0u, 1u << V30_PIN_CLK);
}

static void clock_stop_low(engine_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    gpio_set_function(V30_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(V30_PIN_CLK, false); gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static bool reset_clocks(uint32_t count) {
    for (uint32_t i = 0u; i < count; ++i) {
        uint64_t end = time_us_64() + timeout_us(64u);
        while (!gpio_get(V30_PIN_CLK) && time_us_64() <= end) tight_loop_contents();
        if (!gpio_get(V30_PIN_CLK)) return false;
        end = time_us_64() + timeout_us(64u);
        while (gpio_get(V30_PIN_CLK) && time_us_64() <= end) tight_loop_contents();
        if (gpio_get(V30_PIN_CLK)) return false;
    }
    return true;
}

static void response_init(engine_sm_t *s) {
    s->pio = pio1; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1c_bounded_rom_window_program);
    pio_sm_config c = pc1c_bounded_rom_window_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u); sm_config_set_out_pins(&c, 0u, OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_jmp_pin(&c, V30_PIN_ASTB);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void observer_init(engine_sm_t *s) {
    s->pio = pio0; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1c_sram_rom_execution_observer_program);
    pio_sm_config c = pc1c_sram_rom_execution_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u); sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

static void phase_init(engine_sm_t *s) {
    s->pio = pio0; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u); sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

static void arm(engine_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false); pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm); pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
}

static uint32_t dma_remain(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static int tx_dma(const engine_sm_t *s) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true); channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s->pio, s->sm, true));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)ch, &c, &s->pio->txf[s->sm], g_table_stream,
                          EXECUTION_BUDGET_CYCLES * BLOCK_WORDS, true);
    return ch;
}

static int observer_dma(const engine_sm_t *s) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false); channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(s->pio, s->sm, false));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)ch, &c, g_observer, &s->pio->rxf[s->sm],
                          OBSERVER_WORDS, true);
    return ch;
}

static void stop_dma(int ch) { dma_channel_abort((uint)ch); dma_channel_unclaim((uint)ch); }

static void classify(result_t *r) {
    static const char expected[] = "PI86 BIOS\r\n";
    uint32_t diag = 0u;
    const uint32_t checkpoint = BIOS_BASE + native_bios_rom_size - 2u;
    r->supported_reads_ok = true;
    r->complete_cycles = r->observer_words / 2u;
    for (uint32_t i = 0u; i < r->complete_cycles; ++i) {
        uint32_t a = g_observer[i * 2u], d = g_observer[i * 2u + 1u];
        uint32_t address = decode_address(a); uint16_t expected_word;
        if (memory_read(a) && rom_word(address, &expected_word)) {
            ++r->supported_reads;
            if (decode_ad(d) != expected_word) r->supported_reads_ok = false;
        } else {
            ++r->unsupported_cycles;
        }
        if (address == BIOS_BASE && memory_read(a)) r->far_target_seen = true;
        if (address == checkpoint && memory_read(a)) ++r->checkpoint_reads;
        if (address == DIAGNOSTIC_PORT && io_write(a) && diag + 1u < sizeof r->diagnostic)
            r->diagnostic[diag++] = (char)(decode_ad(d) >> 8);
    }
    r->diagnostic[diag] = '\0';
    r->diagnostic_ok = strcmp(r->diagnostic, expected) == 0;
    r->checkpoint_ok = r->checkpoint_reads >= 4u;
}

static void run(engine_sm_t *clock, engine_sm_t *response,
                engine_sm_t *observer, engine_sm_t *phase, result_t *r) {
    memset(r, 0, sizeof *r); memset(g_observer, 0, sizeof g_observer);
    gpio_put(V30_PIN_RESET, true); ad_to_sio();
    clock_prepare(clock); pio_sm_set_enabled(clock->pio, clock->sm, true);
    r->reset_ok = reset_clocks(RESET_CLOCKS); clock_stop_low(clock);

    clock_prepare(clock); arm(response); arm(observer); arm(phase);
    pio_sm_exec(response->pio, response->sm, pio_encode_mov(pio_pindirs, pio_null));
    for (uint i = 0u; i < 16u; ++i) pio_gpio_init(response->pio, ad_pins[i]);
    int tx = tx_dma(response), obs = observer_dma(observer);
    uint64_t prime_end = time_us_64() + 10000u;
    while (pio_sm_get_tx_fifo_level(response->pio, response->sm) < 4u &&
           time_us_64() <= prime_end) tight_loop_contents();
    r->tx_dma_pre = dma_remain(tx); r->pre_pio1_oe = pio1->dbg_padoe;
    r->pre_pio2_oe = pio2->dbg_padoe;
    r->clean_epoch = r->reset_ok && r->tx_dma_pre > 0u &&
        r->pre_pio1_oe == 0u && r->pre_pio2_oe == (1u << V30_PIN_CLK) &&
        !gpio_get(V30_PIN_CLK);

    pio_enable_sm_mask_in_sync(pio0, (1u << observer->sm) | (1u << phase->sm));
    if (r->clean_epoch) {
        uint32_t irq = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false); pio_sm_set_enabled(response->pio, response->sm, true);
        pio_sm_set_enabled(clock->pio, clock->sm, true);
        uint64_t end = time_us_64() + timeout_us(RUN_TIMEOUT_CLOCKS);
        while (dma_remain(obs) != 0u && time_us_64() <= end) tight_loop_contents();
        uint64_t release_end = time_us_64() + timeout_us(16u);
        while ((pio1->dbg_padoe & V30_AD_BUS_MASK) && time_us_64() <= release_end)
            tight_loop_contents();
        gpio_put(V30_PIN_RESET, true); clock_stop_low(clock); restore_interrupts(irq);
    }
    r->observer_words = OBSERVER_WORDS - dma_remain(obs);
    r->tx_dma_post = dma_remain(tx); r->post_pio1_oe = pio1->dbg_padoe;
    pio_sm_set_enabled(response->pio, response->sm, false);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
    pio_sm_exec(response->pio, response->sm, pio_encode_mov(pio_pindirs, pio_null));
    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           r->phase_count < FIRST_PHASE_COUNT)
        r->phase_raw[r->phase_count++] = pio_sm_get(phase->pio, phase->sm);
    stop_dma(tx); stop_dma(obs); ad_to_sio();
    r->first_address_ok = r->complete_cycles && decode_address(g_observer[0]) == RESET_ROM_BASE;
    r->first_response_ok = r->phase_count == FIRST_PHASE_COUNT &&
        decode_ad(r->phase_raw[3]) == 0x00EAu &&
        decode_ad(r->phase_raw[4]) == 0x00EAu && decode_ad(r->phase_raw[5]) == 0x00EAu;
    classify(r); r->terminal_safe = safe_terminal();
}

static const char *pf(bool value) { return value ? "PASS" : "FAIL"; }

static void print_result(const result_t *r) {
    bool pass = r->reset_ok && r->clean_epoch && r->first_address_ok &&
        r->first_response_ok && r->far_target_seen && r->diagnostic_ok &&
        r->checkpoint_ok && r->supported_reads_ok && r->terminal_safe &&
        r->observer_words == OBSERVER_WORDS;
    printf("\n[V30 BIOS OUTPUT]\n%s\n", r->diagnostic);
    printf("[SUMMARY]\n");
    printf("Measurement epoch        %s\n", pf(r->clean_epoch));
    printf("Reset / FFFF0 fetch      %s\n", pf(r->reset_ok && r->first_address_ok));
    printf("First response 00EA      %s\n", pf(r->first_response_ok));
    printf("F0000 ROM execution      %s\n", pf(r->far_target_seen));
    printf("Current-address reads    %s (%lu hits)\n", pf(r->supported_reads_ok),
           (unsigned long)r->supported_reads);
    printf("Diagnostic I/O 00E9      %s\n", pf(r->diagnostic_ok));
    printf("Checkpoint loop          %s (%lu reads)\n", pf(r->checkpoint_ok),
           (unsigned long)r->checkpoint_reads);
    printf("Bus ownership/safety     %s\n", pf(r->terminal_safe));
    printf("C0C1-B1 RESULT           %s\n", pf(pass));
    printf("\n[ENGINEERING DETAILS]\n");
    printf("PC1-C0C1-B1 Bounded Multi-Cycle ROM - 0.300 MHz\n");
    printf("Table shape              = 32 entries + sentinel\n");
    printf("Execution budget         = %u identical table blocks\n", EXECUTION_BUDGET_CYCLES);
    printf("Current-cycle M33        = NONE\n");
    printf("Observer complete cycles = %lu/%u\n", (unsigned long)r->complete_cycles,
           OBSERVER_CYCLES);
    printf("Unsupported/high-Z cycles= %lu\n", (unsigned long)r->unsupported_cycles);
    printf("DMA remain pre/post      = %lu/%lu\n", (unsigned long)r->tx_dma_pre,
           (unsigned long)r->tx_dma_post);
    printf("PIO1 OE pre/post         = %08lX/%08lX\n",
           (unsigned long)r->pre_pio1_oe, (unsigned long)r->post_pio1_oe);
    printf("ROM image                = %lu bytes; SHA-256 %s\n",
           (unsigned long)native_bios_rom_size, native_bios_rom_sha256);
    printf("TERMINAL SAFE STATE      = %s\n", pf(r->terminal_safe));
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    header_high_z(); controls_init(); ad_to_sio(); prepare_table_stream();
    stdio_init_all(); while (!stdio_usb_connected()) sleep_ms(10); sleep_ms(100);
    engine_sm_t clock, response, observer, phase;
    clock_init(&clock); response_init(&response); observer_init(&observer); phase_init(&phase);
    result_t result; run(&clock, &response, &observer, &phase, &result);
    print_result(&result); fflush(stdout);
    while (true) tight_loop_contents();
}
