/*
 * PC1-C0C1-B2-C same-run multi-slot and byte-lane RAM proof.
 *
 * Epoch A runs the accepted 32-entry current-address ROM engine with RAM
 * cycles deliberately unsupported. Its passive PIO0 trace supplies only the
 * real fetch order. While RESET is asserted, firmware compiles that order
 * into exact PIO keys and swaps PIO1 instruction memory.
 *
 * Epoch B has no M33 work in a current bus cycle. RP2350 PIO v1 indexed
 * PUTGET storage retains two independent words plus one sequential byte-lane
 * slot. Every V30 read is mirrored to a separate unsupported address for
 * passive proof of CPU consumption. This remains bounded, not general RAM.
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

#ifndef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
#include "pc1b_first_cycle_phase_capture.pio.h"
#endif
#include "pc1c_bounded_rom_window.pio.h"
#include "pc1c_reset_clock_qualifier.pio.h"
#include "pc1c_multi_slot_ram.pio.h"
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
#include "pc1c_byte_write_phase_observer.pio.h"
#else
#include "pc1c_sram_rom_execution_observer.pio.h"
#endif
#include "perf_continuous_clock.pio.h"
#include "multi_slot_ram_test_rom.h"
#include "v30/v30_pins.h"

#define V30_HZ                         300000u
#define RESET_CLOCKS                       20u
#define RUN_TIMEOUT_CLOCKS               4096u
#define TABLE_ENTRIES                      32u
#define ENTRY_WORDS                         3u
#define BLOCK_WORDS (TABLE_ENTRIES * ENTRY_WORDS + 1u)
#define EXECUTION_BUDGET_CYCLES           128u
#define OBSERVER_CYCLES                    96u
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
#define OBSERVER_STRIDE                      6u
#else
#define OBSERVER_STRIDE                      2u
#endif
#define OBSERVER_WORDS (OBSERVER_CYCLES * OBSERVER_STRIDE)
#define FIRST_PHASE_COUNT                   6u
#define MAX_SEQUENCE                       96u
#define RESET_ROM_BASE                0xFFFF0u
#define BIOS_BASE                     0xF0000u
#define RAM_CASES                           4u
#define MIRROR_PORT                      0x00E8u
#define CHECKPOINT_WORD_ADDRESS        (BIOS_BASE + 0x0026u)
#define OUT_COUNT                          28u
#define DESC_DRIVE                   (1u << 28)
#define DESC_SLOT(slot)       ((uint32_t)(slot) << 29)

typedef struct { PIO pio; uint sm; uint offset; } engine_sm_t;

typedef struct {
    bool reset_ok, clean_epoch, tx_primed, pio2_clock_only, putget_ok;
    bool clock_low_before_release, first_address_ok, first_response_ok;
    bool far_target_seen, supported_reads_ok, checkpoint_ok, terminal_safe;
    uint32_t pre_pio1_oe, pre_pio2_oe, post_pio1_oe;
    uint32_t tx0_pre, tx0_post, tx1_pre, tx1_post;
    uint32_t fifo0_pre, fifo1_pre, observer_words, complete_cycles;
    uint32_t supported_reads, unsupported_cycles, checkpoint_reads;
    uint32_t qualified_pairs, phase_count, phase_raw[FIRST_PHASE_COUNT];
    uint32_t ram_write_seen_mask, ram_write_mask, ram_read_mask, mirror_write_mask;
    uint16_t ram_write_value[RAM_CASES];
    uint16_t ram_read_value[RAM_CASES];
    uint16_t mirror_write_value[RAM_CASES];
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
    uint32_t ram_write_phase_raw[RAM_CASES][5];
#endif
} result_t;

static const uint32_t ram_address[RAM_CASES] = {
    0x00100u, 0x00102u, 0x00104u, 0x00105u,
};
static const uint16_t expected_bus_value[RAM_CASES] = {
    0x1234u, 0x5678u, 0x0034u, 0x3400u,
};
static const uint16_t expected_mirror_value[RAM_CASES] = {
    0x1234u, 0x5678u, 0x0034u, 0x0034u,
};
static const uint8_t mirror_order[RAM_CASES] = {1u, 0u, 2u, 3u};
static const uint8_t storage_slot[RAM_CASES] = {1u, 2u, 3u, 3u};

static uint32_t g_table_stream[EXECUTION_BUDGET_CYCLES * BLOCK_WORDS];
static uint32_t g_observer[OBSERVER_WORDS];
static uint32_t g_keys[MAX_SEQUENCE + 1u];
static uint32_t g_descriptors[MAX_SEQUENCE];
static uint32_t g_sequence_count;

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

static bool memory_write(uint32_t raw) {
    return bit(raw, V30_PIN_IOM) && bit(raw, V30_PIN_BUFRW) &&
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
    return (1u << V30_PIN_ASTB) | (1u << V30_PIN_IOM) |
           (1u << V30_PIN_INTAK) | encode_address(address);
}

static bool rom_word(uint32_t address, uint16_t *value) {
    static const uint8_t reset_vector[6] = {0xEA, 0x00, 0x00, 0x00, 0xF0, 0x90};
    if (address & 1u) return false;
    if (address >= RESET_ROM_BASE && address < RESET_ROM_BASE + sizeof reset_vector) {
        uint32_t i = address - RESET_ROM_BASE;
        *value = (uint16_t)reset_vector[i] | ((uint16_t)reset_vector[i + 1u] << 8);
        return true;
    }
    if (address >= BIOS_BASE &&
        address + 1u < BIOS_BASE + multi_slot_ram_test_rom_size) {
        uint32_t i = address - BIOS_BASE;
        *value = (uint16_t)multi_slot_ram_test_rom_data[i] |
                 ((uint16_t)multi_slot_ram_test_rom_data[i + 1u] << 8);
        return true;
    }
    return false;
}

static void append_table_entry(uint32_t *block, uint32_t slot,
                               uint32_t address, uint16_t value) {
    block[slot * ENTRY_WORDS] = 0u;
    block[slot * ENTRY_WORDS + 1u] = qualified_key(address);
    block[slot * ENTRY_WORDS + 2u] = encode_word(value);
}

static void prepare_bounded_table(void) {
    const uint32_t image_words = multi_slot_ram_test_rom_size / 2u;
    hard_assert((multi_slot_ram_test_rom_size & 1u) == 0u);
    hard_assert(image_words + 3u <= TABLE_ENTRIES);
    uint32_t prototype[BLOCK_WORDS];
    uint32_t slot = 0u;
    append_table_entry(prototype, slot++, RESET_ROM_BASE, 0x00EAu);
    append_table_entry(prototype, slot++, RESET_ROM_BASE + 2u, 0x0000u);
    append_table_entry(prototype, slot++, RESET_ROM_BASE + 4u, 0x90F0u);
    for (uint32_t i = 0u; i < image_words; ++i) {
        uint16_t value;
        hard_assert(rom_word(BIOS_BASE + i * 2u, &value));
        append_table_entry(prototype, slot++, BIOS_BASE + i * 2u, value);
    }
    while (slot < TABLE_ENTRIES) {
        uint32_t i = slot;
        append_table_entry(prototype, slot++, 0xE0000u + i * 2u,
                           (uint16_t)(0xD000u | i));
    }
    prototype[TABLE_ENTRIES * ENTRY_WORDS] = 1u;
    for (uint32_t cycle = 0u; cycle < EXECUTION_BUDGET_CYCLES; ++cycle)
        memcpy(&g_table_stream[cycle * BLOCK_WORDS], prototype, sizeof prototype);
}

static int ram_case(uint32_t address) {
    for (uint32_t i = 0u; i < RAM_CASES; ++i)
        if (ram_address[i] == address) return (int)i;
    return -1;
}

/* Keep only supported ROM reads and the eight qualified RAM operations.
 * Mirror writes and all other cycles consume no key and cannot authorize
 * PINDIRS. Slot 3 is reused only after the low-byte read has completed. */
static bool compile_dynamic_sequence(uint32_t observer_words) {
    g_sequence_count = 0u;
    uint32_t write_mask = 0u, read_mask = 0u;
    uint32_t cycles = observer_words / OBSERVER_STRIDE;
    for (uint32_t i = 0u; i < cycles && g_sequence_count < MAX_SEQUENCE; ++i) {
        uint32_t raw = g_observer[i * OBSERVER_STRIDE];
        uint32_t address = decode_address(raw);
        uint16_t value;
        uint32_t descriptor = 0u;
        bool include = false;
        if (memory_read(raw) && rom_word(address, &value)) {
            descriptor = encode_word(value) | DESC_DRIVE;
            include = true;
        } else {
            int c = ram_case(address);
            if (c >= 0 && memory_write(raw) && !(write_mask & (1u << c))) {
                descriptor = DESC_SLOT(storage_slot[c]);
                write_mask |= 1u << c; include = true;
            } else if (c >= 0 && memory_read(raw) && !(read_mask & (1u << c))) {
                descriptor = DESC_SLOT(storage_slot[c]) | DESC_DRIVE;
                read_mask |= 1u << c; include = true;
            }
        }
        if (include) {
            g_keys[g_sequence_count] = raw;
            g_descriptors[g_sequence_count] = descriptor;
            ++g_sequence_count;
        }
    }
    /* The final successful match still pulls a successor key. */
    g_keys[g_sequence_count] = 0xFFFFFFFFu;
    return write_mask == 0x0Fu && read_mask == 0x0Fu &&
           g_sequence_count >= 16u &&
           g_sequence_count < MAX_SEQUENCE;
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

static void reset_qualifier_init(engine_sm_t *s) {
    s->pio = pio2; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1c_reset_clock_qualifier_program);
    pio_sm_config c = pc1c_reset_clock_qualifier_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

static bool wait_reset_qualification(const engine_sm_t *s) {
    uint64_t end = time_us_64() + timeout_us(RESET_CLOCKS + 64u);
    while (pio_sm_is_rx_fifo_empty(s->pio, s->sm) && time_us_64() <= end)
        tight_loop_contents();
    if (pio_sm_is_rx_fifo_empty(s->pio, s->sm)) return false;
    (void)pio_sm_get(s->pio, s->sm);
    return true;
}

static void observer_init(engine_sm_t *s) {
    s->pio = pio0; s->sm = pio_claim_unused_sm(s->pio, true);
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
    s->offset = pio_add_program(s->pio, &pc1c_byte_write_phase_observer_program);
    pio_sm_config c = pc1c_byte_write_phase_observer_program_get_default_config(s->offset);
#else
    s->offset = pio_add_program(s->pio, &pc1c_sram_rom_execution_observer_program);
    pio_sm_config c = pc1c_sram_rom_execution_observer_program_get_default_config(s->offset);
#endif
    sm_config_set_in_pins(&c, 0u); sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

#ifndef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
static void phase_init(engine_sm_t *s) {
    s->pio = pio0; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u); sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}
#endif

static void arm(engine_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false); pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm); pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
}

/* The SDK's generic FIFO-clear helper toggles the legacy FJOIN_RX bit. After
 * using it to empty the descriptor queue, explicitly restore the RP2350 v1
 * PUTGET storage mode and verify the hardware-visible configuration. */
static bool restore_putget(engine_sm_t *s) {
    const uint32_t mask = PIO_SM0_SHIFTCTRL_FJOIN_TX_BITS |
                          PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS |
                          PIO_SM0_SHIFTCTRL_FJOIN_RX_PUT_BITS |
                          PIO_SM0_SHIFTCTRL_FJOIN_RX_GET_BITS;
    const uint32_t value = PIO_SM0_SHIFTCTRL_FJOIN_RX_PUT_BITS |
                           PIO_SM0_SHIFTCTRL_FJOIN_RX_GET_BITS;
    hw_write_masked(&s->pio->sm[s->sm].shiftctrl, value, mask);
    return (s->pio->sm[s->sm].shiftctrl & mask) == value;
}

static uint32_t dma_remain(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static int tx_dma(const engine_sm_t *s, const uint32_t *source, uint32_t words) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s->pio, s->sm, true));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)ch, &c, &s->pio->txf[s->sm], source, words, true);
    return ch;
}

static int observer_dma(const engine_sm_t *s) {
    int ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(s->pio, s->sm, false));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)ch, &c, g_observer, &s->pio->rxf[s->sm],
                          OBSERVER_WORDS, true);
    return ch;
}

static void stop_dma(int ch) {
    dma_channel_abort((uint)ch); dma_channel_unclaim((uint)ch);
}

static void qualify_reset(engine_sm_t *clock, engine_sm_t *qualifier,
                          result_t *r) {
    gpio_put(V30_PIN_RESET, true); ad_to_sio();
    clock_prepare(clock); arm(qualifier);
    pio_enable_sm_mask_in_sync(pio2, (1u << clock->sm) | (1u << qualifier->sm));
    r->reset_ok = wait_reset_qualification(qualifier);
    clock_stop_low(clock); pio_sm_set_enabled(qualifier->pio, qualifier->sm, false);
}

static void classify(result_t *r) {
    r->supported_reads_ok = true;
    r->complete_cycles = r->observer_words / OBSERVER_STRIDE;
    uint32_t mirror_ordinal = 0u;
    for (uint32_t i = 0u; i < r->complete_cycles; ++i) {
        uint32_t base = i * OBSERVER_STRIDE;
        uint32_t a = g_observer[base];
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
        uint32_t d = g_observer[base + 3u]; /* R2, retained for baseline comparison. */
#else
        uint32_t d = g_observer[base + 1u];
#endif
        uint32_t address = decode_address(a);
        uint16_t expected;
        if (memory_read(a) && rom_word(address, &expected)) {
            ++r->supported_reads;
            if (decode_ad(d) != expected) r->supported_reads_ok = false;
        } else {
            ++r->unsupported_cycles;
        }
        if (memory_read(a) && address == BIOS_BASE) r->far_target_seen = true;
        if (memory_read(a) && address == CHECKPOINT_WORD_ADDRESS)
            ++r->checkpoint_reads;
        int c = ram_case(address);
        if (c >= 0 && memory_write(a)) {
            r->ram_write_seen_mask |= 1u << c;
            r->ram_write_value[c] = decode_ad(d);
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
            for (uint32_t p = 0u; p < 5u; ++p)
                r->ram_write_phase_raw[c][p] = g_observer[base + 1u + p];
#endif
            if (r->ram_write_value[c] == expected_bus_value[c])
                r->ram_write_mask |= 1u << c;
        }
        for (uint32_t m = 0u; m < RAM_CASES; ++m) {
            if (memory_read(a) && address == ram_address[m]) {
                r->ram_read_value[m] = decode_ad(d);
                if (r->ram_read_value[m] == expected_bus_value[m])
                    r->ram_read_mask |= 1u << m;
            }
        }
        if (io_write(a) && address == MIRROR_PORT &&
            mirror_ordinal < RAM_CASES) {
            uint32_t m = mirror_order[mirror_ordinal++];
            r->mirror_write_value[m] = decode_ad(d);
            if (r->mirror_write_value[m] == expected_mirror_value[m])
                r->mirror_write_mask |= 1u << m;
        }
    }
    r->checkpoint_ok = r->checkpoint_reads >= 4u;
    r->first_address_ok = r->complete_cycles &&
        decode_address(g_observer[0]) == RESET_ROM_BASE;
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
    if (r->complete_cycles) {
        for (uint32_t p = 0u; p < 5u; ++p)
            r->phase_raw[p + 1u] = g_observer[1u + p];
        r->phase_count = FIRST_PHASE_COUNT;
    }
    r->first_response_ok = r->phase_count == FIRST_PHASE_COUNT &&
        decode_ad(r->phase_raw[3]) == 0x00EAu &&
        decode_ad(r->phase_raw[4]) == 0x00EAu &&
        decode_ad(r->phase_raw[5]) == 0x00EAu;
#else
    r->first_response_ok = r->phase_count == FIRST_PHASE_COUNT &&
        decode_ad(r->phase_raw[3]) == 0x00EAu &&
        decode_ad(r->phase_raw[4]) == 0x00EAu &&
        decode_ad(r->phase_raw[5]) == 0x00EAu;
#endif
    r->terminal_safe = safe_terminal();
}

static void finish_observation(engine_sm_t *response0, engine_sm_t *response1,
                               engine_sm_t *observer, engine_sm_t *phase,
                               int obs, result_t *r) {
    r->observer_words = OBSERVER_WORDS - dma_remain(obs);
    r->post_pio1_oe = pio1->dbg_padoe;
    if (response0) pio_sm_set_enabled(response0->pio, response0->sm, false);
    if (response1) pio_sm_set_enabled(response1->pio, response1->sm, false);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
#ifndef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
    pio_sm_set_enabled(phase->pio, phase->sm, false);
    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           r->phase_count < FIRST_PHASE_COUNT)
        r->phase_raw[r->phase_count++] = pio_sm_get(phase->pio, phase->sm);
#else
    (void)phase;
#endif
    stop_dma(obs); ad_to_sio(); classify(r);
}

static void bounded_response_init(engine_sm_t *s) {
    s->pio = pio1; s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1c_bounded_rom_window_program);
    pio_sm_config c = pc1c_bounded_rom_window_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u); sm_config_set_out_pins(&c, 0u, OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_jmp_pin(&c, V30_PIN_ASTB);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

static void run_learn(engine_sm_t *clock, engine_sm_t *response,
                      engine_sm_t *qualifier, engine_sm_t *observer,
                      engine_sm_t *phase, result_t *r) {
    memset(r, 0, sizeof *r); memset(g_observer, 0, sizeof g_observer);
    qualify_reset(clock, qualifier, r);
    clock_prepare(clock); arm(response); arm(observer);
#ifndef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
    arm(phase);
#endif
    pio_sm_exec(response->pio, response->sm, pio_encode_mov(pio_pindirs, pio_null));
    for (uint i = 0u; i < 16u; ++i) pio_gpio_init(response->pio, ad_pins[i]);
    int tx = tx_dma(response, g_table_stream,
                    EXECUTION_BUDGET_CYCLES * BLOCK_WORDS);
    int obs = observer_dma(observer);
    uint64_t end = time_us_64() + 10000u;
    while (pio_sm_get_tx_fifo_level(response->pio, response->sm) < 4u &&
           time_us_64() <= end) tight_loop_contents();
    r->fifo0_pre = pio_sm_get_tx_fifo_level(response->pio, response->sm);
    r->tx_primed = r->fifo0_pre == 4u;
    r->tx0_pre = dma_remain(tx); r->pre_pio1_oe = pio1->dbg_padoe;
    r->pre_pio2_oe = pio2->dbg_padoe;
    r->pio2_clock_only = r->pre_pio2_oe == (1u << V30_PIN_CLK);
    r->clock_low_before_release = !gpio_get(V30_PIN_CLK);
    r->clean_epoch = r->reset_ok && r->tx_primed && r->tx0_pre > 0u &&
        r->pre_pio1_oe == 0u && r->pio2_clock_only && r->clock_low_before_release;
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
    pio_sm_set_enabled(pio0, observer->sm, true);
#else
    pio_enable_sm_mask_in_sync(pio0, (1u << observer->sm) | (1u << phase->sm));
#endif
    if (r->clean_epoch) {
        uint32_t irq = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_sm_set_enabled(response->pio, response->sm, true);
        pio_sm_set_enabled(clock->pio, clock->sm, true);
        uint64_t deadline = time_us_64() + timeout_us(RUN_TIMEOUT_CLOCKS);
        while (dma_remain(obs) && time_us_64() <= deadline) tight_loop_contents();
        gpio_put(V30_PIN_RESET, true); clock_stop_low(clock);
        restore_interrupts(irq);
    }
    r->tx0_post = dma_remain(tx);
    pio_sm_set_enabled(response->pio, response->sm, false);
    pio_sm_exec(response->pio, response->sm, pio_encode_mov(pio_pindirs, pio_null));
    stop_dma(tx);
    finish_observation(response, NULL, observer, phase, obs, r);
}

static void remove_bounded_response(engine_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_unclaim(s->pio, s->sm);
    pio_remove_program(s->pio, &pc1c_bounded_rom_window_program, s->offset);
}

static void dynamic_response_init(engine_sm_t *matcher, engine_sm_t *responder) {
    matcher->pio = pio1; matcher->sm = pio_claim_unused_sm(pio1, true);
    matcher->offset = pio_add_program(pio1, &pc1c_multi_slot_ram_matcher_program);
    responder->pio = pio1; responder->sm = pio_claim_unused_sm(pio1, true);
    responder->offset = pio_add_program(pio1, &pc1c_multi_slot_ram_responder_program);
    hard_assert(pc1c_multi_slot_ram_matcher_program.length +
                pc1c_multi_slot_ram_responder_program.length == 32u);
    pio_sm_config m = pc1c_multi_slot_ram_matcher_program_get_default_config(matcher->offset);
    sm_config_set_in_pins(&m, 0u);
    hard_assert(pio_sm_init(pio1, matcher->sm, matcher->offset, &m) == PICO_OK);
    pio_sm_config d = pc1c_multi_slot_ram_responder_program_get_default_config(responder->offset);
    sm_config_set_in_pins(&d, 0u); sm_config_set_out_pins(&d, 0u, OUT_COUNT);
    sm_config_set_out_shift(&d, true, false, 32u);
    sm_config_set_fifo_join(&d, PIO_FIFO_JOIN_PUTGET);
    hard_assert(pio_sm_init(pio1, responder->sm, responder->offset, &d) == PICO_OK);
}

static void run_dynamic(engine_sm_t *clock, engine_sm_t *matcher,
                        engine_sm_t *responder, engine_sm_t *qualifier,
                        engine_sm_t *observer, engine_sm_t *phase, result_t *r) {
    memset(r, 0, sizeof *r); memset(g_observer, 0, sizeof g_observer);
    qualify_reset(clock, qualifier, r);
    clock_prepare(clock); arm(matcher); arm(responder); arm(observer); arm(phase);
    r->putget_ok = restore_putget(responder);
    pio_sm_exec(responder->pio, responder->sm, pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_exec(responder->pio, responder->sm, pio_encode_mov(pio_isr, pio_null));
    pio_interrupt_clear(pio1, 0u);
    for (uint i = 0u; i < 16u; ++i) pio_gpio_init(pio1, ad_pins[i]);

    /* Preload key zero into Y; DMA supplies keys 1..N including the terminal. */
    pio_sm_put_blocking(matcher->pio, matcher->sm, g_keys[0]);
    pio_sm_exec(matcher->pio, matcher->sm, pio_encode_pull(false, false));
    pio_sm_exec(matcher->pio, matcher->sm, pio_encode_mov(pio_y, pio_osr));
    int key_dma = tx_dma(matcher, &g_keys[1], g_sequence_count);
    int desc_dma = tx_dma(responder, g_descriptors, g_sequence_count);
    int obs = observer_dma(observer);
    uint64_t end = time_us_64() + 10000u;
    while ((pio_sm_get_tx_fifo_level(matcher->pio, matcher->sm) < 4u ||
            pio_sm_get_tx_fifo_level(responder->pio, responder->sm) < 4u) &&
           time_us_64() <= end) tight_loop_contents();
    r->fifo0_pre = pio_sm_get_tx_fifo_level(matcher->pio, matcher->sm);
    r->fifo1_pre = pio_sm_get_tx_fifo_level(responder->pio, responder->sm);
    r->tx_primed = r->fifo0_pre == 4u && r->fifo1_pre == 4u;
    r->tx0_pre = dma_remain(key_dma); r->tx1_pre = dma_remain(desc_dma);
    r->pre_pio1_oe = pio1->dbg_padoe; r->pre_pio2_oe = pio2->dbg_padoe;
    r->pio2_clock_only = r->pre_pio2_oe == (1u << V30_PIN_CLK);
    r->clock_low_before_release = !gpio_get(V30_PIN_CLK);
    r->clean_epoch = r->reset_ok && r->tx_primed && r->tx0_pre > 0u &&
        r->tx1_pre > 0u && r->pre_pio1_oe == 0u && r->pio2_clock_only &&
        r->putget_ok &&
        r->clock_low_before_release;
    pio_enable_sm_mask_in_sync(pio0, (1u << observer->sm) | (1u << phase->sm));
    if (r->clean_epoch) {
        uint32_t irq = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_enable_sm_mask_in_sync(pio1,
            (1u << matcher->sm) | (1u << responder->sm));
        pio_sm_set_enabled(clock->pio, clock->sm, true);
        uint64_t deadline = time_us_64() + timeout_us(RUN_TIMEOUT_CLOCKS);
        while (dma_remain(obs) && time_us_64() <= deadline) tight_loop_contents();
        uint64_t release = time_us_64() + timeout_us(16u);
        while ((pio1->dbg_padoe & V30_AD_BUS_MASK) && time_us_64() <= release)
            tight_loop_contents();
        gpio_put(V30_PIN_RESET, true); clock_stop_low(clock);
        restore_interrupts(irq);
    }
    r->tx0_post = dma_remain(key_dma); r->tx1_post = dma_remain(desc_dma);
    r->qualified_pairs = g_sequence_count - r->tx1_post;
    pio_sm_set_enabled(matcher->pio, matcher->sm, false);
    pio_sm_set_enabled(responder->pio, responder->sm, false);
    pio_sm_exec(responder->pio, responder->sm, pio_encode_mov(pio_pindirs, pio_null));
    stop_dma(key_dma); stop_dma(desc_dma);
    finish_observation(matcher, responder, observer, phase, obs, r);
}

static const char *pf(bool value) { return value ? "PASS" : "FAIL"; }

static void print_result(const result_t *r, const char *epoch, bool dynamic) {
    bool pass = r->reset_ok && r->clean_epoch && r->first_address_ok &&
        r->first_response_ok && r->far_target_seen && r->supported_reads_ok &&
        r->ram_write_mask == 0x0Fu && r->checkpoint_ok && r->terminal_safe &&
        r->observer_words == OBSERVER_WORDS;
    if (dynamic) pass = pass && r->ram_read_mask == 0x0Fu &&
        r->mirror_write_mask == 0x0Fu &&
        r->qualified_pairs == g_sequence_count && r->tx0_post == 0u &&
        r->tx1_post == 0u;
    printf("\n[%s SUMMARY]\n", epoch);
    printf("Measurement epoch        %s\n", pf(r->clean_epoch));
    printf("Reset / FFFF0 fetch      %s\n", pf(r->reset_ok && r->first_address_ok));
    printf("First response 00EA      %s\n", pf(r->first_response_ok));
    printf("F0000 ROM execution      %s\n", pf(r->far_target_seen));
    printf("ROM response data        %s (%lu reads)\n", pf(r->supported_reads_ok),
           (unsigned long)r->supported_reads);
    static const char *const label[RAM_CASES] = {
        "WORD0", "WORD1", "LOW8 ", "HIGH8",
    };
    for (uint32_t i = 0u; i < RAM_CASES; ++i) {
        printf("%s write %05lX=%04X   %s (observed %04X)\n", label[i],
               (unsigned long)ram_address[i], expected_bus_value[i],
               pf(r->ram_write_mask & (1u << i)), r->ram_write_value[i]);
        printf("%s read  %05lX=%04X   %s (observed %04X)\n", label[i],
               (unsigned long)ram_address[i], expected_bus_value[i],
               dynamic ? pf(r->ram_read_mask & (1u << i)) : "LEARN ONLY",
               r->ram_read_value[i]);
        printf("%s OUT   %04X=%04X    %s (observed %04X)\n", label[i],
               MIRROR_PORT, expected_mirror_value[i],
               dynamic ? pf(r->mirror_write_mask & (1u << i)) : "LEARN ONLY",
               r->mirror_write_value[i]);
    }
    printf("Checkpoint loop          %s (%lu reads)\n", pf(r->checkpoint_ok),
           (unsigned long)r->checkpoint_reads);
    printf("Bus ownership/safety     %s\n", pf(r->terminal_safe));
    printf("C0C1-B2-C %s RESULT      %s\n", epoch, pf(pass));
    printf("\n[ENGINEERING DETAILS]\n");
    printf("PC1-C0C1-B2-C Multi-Slot / Byte-Lane RAM - 0.300 MHz\n");
    printf("Epoch                    = %s\n", epoch);
    printf("Current-cycle M33        = NONE\n");
    printf("PIO1 program             = %s\n",
           dynamic ? "exact matcher + indexed PUTGET responder (8+24 words)" :
                     "bounded 32-entry ROM selector (32 words)");
    printf("PIO-local RAM slots      = WORD0:1 WORD1:2 BYTE:3\n");
    printf("Indexed PUTGET storage   = %s\n",
           dynamic ? pf(r->putget_ok) : "NOT USED");
    printf("Learned exact pairs      = %lu\n", (unsigned long)g_sequence_count);
    printf("PIO-qualified pairs      = %lu/%lu\n", (unsigned long)r->qualified_pairs,
           (unsigned long)g_sequence_count);
    printf("RESET clock qualification= %s\n", pf(r->reset_ok));
    printf("TX FIFO primed           = %lu/%lu %s\n",
           (unsigned long)r->fifo0_pre, (unsigned long)r->fifo1_pre,
           pf(r->tx_primed));
    printf("PIO1 pre-release OE      = %08lX %s\n",
           (unsigned long)r->pre_pio1_oe, pf(r->pre_pio1_oe == 0u));
    printf("PIO2 pre-release OE      = %08lX CLK-ONLY %s\n",
           (unsigned long)r->pre_pio2_oe, pf(r->pio2_clock_only));
    printf("CLK stopped LOW          = %s\n", pf(r->clock_low_before_release));
    printf("Observer complete cycles = %lu/%u\n",
           (unsigned long)r->complete_cycles, OBSERVER_CYCLES);
    printf("Non-ROM/other cycles     = %lu\n", (unsigned long)r->unsupported_cycles);
    printf("DMA remain key/desc      = %lu/%lu -> %lu/%lu\n",
           (unsigned long)r->tx0_pre, (unsigned long)r->tx1_pre,
           (unsigned long)r->tx0_post, (unsigned long)r->tx1_post);
    printf("ROM image                = %lu bytes; SHA-256 %s\n",
           (unsigned long)multi_slot_ram_test_rom_size,
           multi_slot_ram_test_rom_sha256);
    printf("TERMINAL SAFE STATE      = %s\n", pf(r->terminal_safe));
    printf("\n[FIRST-CYCLE PHASE TRACE]\n");
    static const char *const names[FIRST_PHASE_COUNT] =
        {"AF", "R1", "F1", "R2", "F2", "R3"};
    for (uint32_t i = 0u; i < r->phase_count; ++i)
        printf("%s raw=%08lX ASTB=%lu CLK=%lu AD=%04X\n", names[i],
               (unsigned long)r->phase_raw[i],
               (unsigned long)bit(r->phase_raw[i], V30_PIN_ASTB),
               (unsigned long)bit(r->phase_raw[i], V30_PIN_CLK),
               decode_ad(r->phase_raw[i]));
    printf("\n[PASSIVE ADDRESS / R2-DATA TRACE]\n");
    uint32_t shown = r->complete_cycles < 48u ? r->complete_cycles : 48u;
    for (uint32_t i = 0u; i < shown; ++i) {
        uint32_t base = i * OBSERVER_STRIDE;
        uint32_t a = g_observer[base];
#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
        uint32_t d = g_observer[base + 3u];
#else
        uint32_t d = g_observer[base + 1u];
#endif
        printf("%02lu addr=%05lX type=%s data=%04X addr_raw=%08lX data_raw=%08lX\n",
               (unsigned long)i, (unsigned long)decode_address(a),
               memory_read(a) ? "MEMR" : memory_write(a) ? "MEMW" :
               io_write(a) ? "IOW" : "OTHER",
               decode_ad(d), (unsigned long)a, (unsigned long)d);
    }
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
static void print_byte_write_phase_result(const result_t *r) {
    bool pass = r->reset_ok && r->clean_epoch && r->first_address_ok &&
        r->first_response_ok && r->far_target_seen && r->supported_reads_ok &&
        (r->ram_write_seen_mask & 0x0Fu) == 0x0Fu &&
        (r->ram_write_mask & 0x03u) == 0x03u && r->checkpoint_ok &&
        r->observer_words == OBSERVER_WORDS && r->terminal_safe;

    printf("\n[SUMMARY]\n");
    printf("Measurement epoch        %s\n", pf(r->clean_epoch));
    printf("Reset / FFFF0 fetch      %s\n", pf(r->reset_ok && r->first_address_ok));
    printf("First response 00EA      %s\n", pf(r->first_response_ok));
    printf("F0000 ROM execution      %s\n", pf(r->far_target_seen));
    printf("ROM response data        %s (%lu reads)\n", pf(r->supported_reads_ok),
           (unsigned long)r->supported_reads);
    printf("WORD writes at R2        %s (%04X / %04X)\n",
           pf((r->ram_write_mask & 0x03u) == 0x03u),
           r->ram_write_value[0], r->ram_write_value[1]);
    printf("LOW8 write cycle seen    %s\n", pf(r->ram_write_seen_mask & (1u << 2)));
    printf("HIGH8 write cycle seen   %s\n", pf(r->ram_write_seen_mask & (1u << 3)));
    printf("Checkpoint loop          %s (%lu reads)\n", pf(r->checkpoint_ok),
           (unsigned long)r->checkpoint_reads);
    printf("Bus ownership/safety     %s\n", pf(r->terminal_safe));
    printf("C0C1-B2-C0 RESULT        %s\n", pf(pass));

    printf("\n[ENGINEERING DETAILS]\n");
    printf("PC1-C0C1-B2-C0 Passive Byte-Write Phase Characterization - 0.300 MHz\n");
    printf("Response engine          = bounded internal-SRAM ROM only\n");
    printf("Byte-write observer      = passive PIO0 -> DMA -> internal SRAM\n");
    printf("Captured phases          = R1 F1 R2 F2 R3\n");
    printf("RAM drive policy         = NONE\n");
    printf("Input synchronizers      = SDK defaults\n");
    printf("Observer complete cycles = %lu/%u\n",
           (unsigned long)r->complete_cycles, OBSERVER_CYCLES);

    static const char *const phase_name[5] = {"R1", "F1", "R2", "F2", "R3"};
    static const char *const case_name[2] = {"LOW8 00104", "HIGH8 00105"};
    for (uint32_t c = 2u; c < 4u; ++c) {
        printf("\n[%s WRITE PHASE TRACE]\n", case_name[c - 2u]);
        for (uint32_t p = 0u; p < 5u; ++p) {
            uint32_t raw = r->ram_write_phase_raw[c][p];
            printf("%s raw=%08lX ASTB=%lu CLK=%lu AD=%04X\n",
                   phase_name[p], (unsigned long)raw,
                   (unsigned long)bit(raw, V30_PIN_ASTB),
                   (unsigned long)bit(raw, V30_PIN_CLK), decode_ad(raw));
        }
    }
    printf("\nInterpretation: compare LOW8 AD7:0 and HIGH8 AD15:8 across phases.\n");
    printf("No RAM response was driven; C0C1-B2-C remains open.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}
#endif

#ifdef PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION
int main(void) {
    header_high_z(); controls_init(); ad_to_sio(); prepare_bounded_table();
    stdio_init_all(); while (!stdio_usb_connected()) sleep_ms(10); sleep_ms(100);
    engine_sm_t clock, qualifier, observer, bounded;
    clock_init(&clock); reset_qualifier_init(&qualifier);
    observer_init(&observer); bounded_response_init(&bounded);
    result_t result;
    run_learn(&clock, &bounded, &qualifier, &observer, NULL, &result);
    print_byte_write_phase_result(&result); fflush(stdout);
    while (true) tight_loop_contents();
}
#else
int main(void) {
    header_high_z(); controls_init(); ad_to_sio(); prepare_bounded_table();
    stdio_init_all(); while (!stdio_usb_connected()) sleep_ms(10); sleep_ms(100);
    engine_sm_t clock, qualifier, observer, phase, bounded;
    clock_init(&clock); reset_qualifier_init(&qualifier);
    observer_init(&observer); phase_init(&phase); bounded_response_init(&bounded);
    result_t learn, replay;
    run_learn(&clock, &bounded, &qualifier, &observer, &phase, &learn);
    bool sequence_ok = compile_dynamic_sequence(learn.observer_words);
    print_result(&learn, "EPOCH-A LEARN", false); fflush(stdout);

    if (sequence_ok && learn.reset_ok && learn.clean_epoch &&
        learn.first_address_ok && learn.first_response_ok &&
        learn.far_target_seen && learn.supported_reads_ok &&
        learn.ram_write_mask == 0x0Fu && learn.checkpoint_ok &&
        learn.terminal_safe) {
        remove_bounded_response(&bounded);
        engine_sm_t matcher, responder;
        dynamic_response_init(&matcher, &responder);
        run_dynamic(&clock, &matcher, &responder, &qualifier, &observer, &phase,
                    &replay);
        print_result(&replay, "EPOCH-B SAME-RUN", true);
        bool overall = replay.reset_ok && replay.clean_epoch &&
            replay.first_address_ok && replay.first_response_ok &&
            replay.far_target_seen && replay.supported_reads_ok &&
            replay.ram_write_mask == 0x0Fu && replay.ram_read_mask == 0x0Fu &&
            replay.mirror_write_mask == 0x0Fu &&
            replay.checkpoint_ok && replay.qualified_pairs == g_sequence_count &&
            replay.tx0_post == 0u && replay.tx1_post == 0u &&
            replay.observer_words == OBSERVER_WORDS && replay.terminal_safe;
        printf("\nC0C1-B2-C OVERALL RESULT = %s\n", pf(overall));
    } else {
        printf("\nEPOCH-B SAME-RUN = NOT RUN (learning/compile gate failed)\n");
        printf("C0C1-B2-C OVERALL RESULT = FAIL\n");
    }
    fflush(stdout);
    while (true) tight_loop_contents();
}
#endif
