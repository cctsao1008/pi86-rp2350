/*
 * PC1-C0C0 descriptor-fed SRAM micro-ROM execution at 0.300 MHz.
 *
 * PIO1 owns the clock, exact early-T1 matching, and scattered AD/PINDIRS.
 * Two DMA channels feed bounded key/response tables from SRAM, so the M33 is
 * absent from every current-cycle response. PIO0 passively captures address,
 * control, and R2 data snapshots through a third DMA channel.
 *
 * This source builds two permanent golden-HAT regressions:
 *   pc1c_sram_rom_execution - memory-write signatures and JMP $ checkpoint;
 *   pc1c_native_bios_hello  - OUT 00E9h, AL diagnostic message and checkpoint.
 * It also hosts pc1c_native_bios_foundation, which exercises the structured
 * BIOS source tree through the same accepted descriptor-fed engine.
 *
 * Architectural boundary:
 *   The key/response order is compiled before RESET release. A physical cycle
 *   must still match its exact address/control key, but this is not the
 *   arbitrary-address PC1-C0C1 service. Unexpected cycles remain high-Z and do
 *   not advance the descriptor streams. Do not add an M33 lookup, SIO-targeted
 *   DMA, or speculative fallback to the current-cycle path.
 *
 * Safety boundary:
 *   Before and after the measurement, RESET is high, CLK is low, and every AD
 *   pin is SIO input/high-Z. PIO1 may own AD only for a matched memory-read data
 *   phase. See docs/pc1c_rom_execution_plan.md and ADR 0003.
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

#include "pc1b_first_cycle_phase_capture.pio.h"
#include "pc1c_pio_rom_sequencer.pio.h"
#include "pc1c_sram_rom_execution_observer.pio.h"

/* The same bus engine is compiled against either the signature ROM or the
 * Native BIOS HELLO ROM. CMake overrides these four symbols for the latter. */
#ifndef PC1C_ROM_HEADER
#define PC1C_ROM_HEADER "pc1c0c_sram_rom.h"
#define PC1C_ROM_DATA pc1c0c_sram_rom_data
#define PC1C_ROM_SIZE pc1c0c_sram_rom_size
#define PC1C_ROM_SHA256 pc1c0c_sram_rom_sha256
#endif
#include PC1C_ROM_HEADER
#include "perf_continuous_clock.pio.h"
#include "v30/v30_pins.h"

/* Test envelope and bounded evidence storage. */
#define PC1C0C_V30_HZ                 300000u
#define RESET_CLOCKS                      20u
#define SIGNAL_TIMEOUT_CLOCKS             64u
#define RUN_TIMEOUT_CLOCKS              4096u
#define OBSERVER_CYCLES                  256u
#define OBSERVER_WORDS    (OBSERVER_CYCLES * 2u)
#define OBSERVER_PRINT_DEPTH              40u
#define FIRST_PHASE_COUNT                  6u
/*
 * PIO1 response descriptor, shifted right by the responder SM:
 *   bits  0..27 = GPIO0..27 output bitmap;
 *   bit      28 = response valid / enable AD PINDIRS.
 */
#define OUT_BASE                           0u
#define OUT_COUNT                         28u
#define RESPONSE_VALID_BIT                28u

/* CPU-visible ROM windows and maximum known-path descriptor count. */
#define RESET_ROM_BASE               0xFFFF0u
#define RESET_ROM_SIZE                     6u
#define V30_ROM_BASE                  0xF0000u
#define SEQUENCE_MAX                      32u
#define CHECKPOINT_RESPONSES               4u
#define DIAGNOSTIC_PORT                0x00E9u
#define DIAGNOSTIC_MAX_BYTES               32u

/* Diagnostic-bearing ROM builds share the electrical and trace gates but
 * retain distinct CPU-visible messages and engineering titles. */
#if defined(PC1C_NATIVE_BIOS_HELLO)
#define PC1C_DIAGNOSTIC_BUILD 1
#define PC1C_RUN_TITLE \
    "PC1-C0C0-H Descriptor-Fed Native BIOS Hello - 0.300 MHz"
static const char expected_diagnostic[] = "HELLO RP2350\r\n";
#elif defined(PC1C_NATIVE_BIOS_FOUNDATION)
#define PC1C_DIAGNOSTIC_BUILD 1
#define PC1C_RUN_TITLE \
    "PC1-C0C0-B Native BIOS Foundation Regression - 0.300 MHz"
static const char expected_diagnostic[] = "PI86 BIOS\r\n";
#endif
/* Exact early-T1 key for a word memory read: ASTB=1, IOM=1, INTAK=1,
 * BUFRW=0, A0=0, and UBE=0. Zero-valued fields are enforced implicitly
 * because the full raw GPIO bank is compared, not by this OR mask alone. */
#define QUALIFIED_T1_CONTROL_BITS ((1u << V30_PIN_ASTB) | \
                                   (1u << V30_PIN_IOM) | \
                                   (1u << V30_PIN_INTAK))

typedef enum {
    /* Values deliberately form a two-bit low/high lane mask. */
    LANES_NONE = 0,
    LANE_LOW = 1,
    LANE_HIGH = 2,
    LANES_WORD = 3,
} lane_mask_t;

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} pc1c_sm_t;

typedef struct {
    /* One decoded pair from the passive PIO0 observer DMA stream. */
    uint32_t address_raw;
    uint32_t data_raw;
    uint16_t response;
    lane_mask_t lanes;
    bool memory_read;
    bool memory_write;
    bool io_write;
    bool rom_hit;
} bus_trace_t;

typedef struct {
    /* Qualification and CPU-visible discriminators. */
    bool reset_ok;
    bool pre_release_clean;
    bool clock_direction_armed;
    bool first_address_ok;
    bool first_memory_read;
    bool first_response_phase_ok;
    bool dma_observer_first_ok;
    bool far_target_seen;
    bool signature_1234;
    bool signature_5678;
    bool signature_abcd;
    bool diagnostic_ok;
    bool checkpoint_ok;
    bool dma_streams_complete;
    bool observer_tail_valid;
    bool terminal_safe;

    /* Pre/post resource state proves the run was cleanly armed and drained. */
    uint32_t pre_pio1_padoe;
    uint32_t matcher_fifo_pre;
    uint32_t responder_fifo_pre;
    uint32_t matcher_fifo_post;
    uint32_t responder_fifo_post;
    uint32_t matcher_dma_pre;
    uint32_t responder_dma_pre;
    uint32_t matcher_dma_post;
    uint32_t responder_dma_post;
    uint32_t observer_dma_pre;
    uint32_t observer_dma_post;
    uint32_t observer_fifo_residue;

    /* Derived evidence counters; none are used to drive the physical bus. */
    uint32_t qualified_pairs;
    uint32_t rom_hits;
    uint32_t unsupported_cycles;
    uint32_t deadline_misses;
    uint32_t unqualified_drive_commands;
    uint32_t checkpoint_reads;
    uint32_t diagnostic_bytes;
    char diagnostic[DIAGNOSTIC_MAX_BYTES + 1u];

    /* Raw observer/phase evidence retained until the run is over. */
    uint32_t observer_words;
    uint32_t observer_trailing_words;
    uint32_t phase_raw[FIRST_PHASE_COUNT];
    uint phase_count;
    bus_trace_t trace[OBSERVER_CYCLES];
    uint trace_count;
} pc1c0c_result_t;

/* Paired, immutable while RESET is released. Index n in both arrays describes
 * one expected ROM read. DMA only transports these words to PIO1. */
static uint32_t g_sequence_keys[SEQUENCE_MAX];
static uint32_t g_sequence_responses[SEQUENCE_MAX];
static uint32_t g_sequence_count;
static uint32_t g_checkpoint_address;
static uint32_t g_observer_dma_words[OBSERVER_WORDS];

static const uint8_t ad_pins[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

/* Raw snapshots are whole GPIO banks. All V30 decode must go through the
 * canonical scattered-pin map rather than assuming AD is numerically packed. */
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
    /* During ASTB-high T1, AD15:AD0 still carries A15:A0. */
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static lane_mask_t decode_lanes(uint32_t raw) {
    /* V30 byte-lane truth table sampled during the address phase:
     *   A0=0, UBE=0 -> 16-bit word; A0=0, UBE=1 -> low byte;
     *   A0=1, UBE=0 -> high byte; A0=1, UBE=1 -> no active lane. */
    const bool a0 = sample_bit(raw, V30_PIN_AD0) != 0u;
    const bool ube_n = sample_bit(raw, V30_PIN_UBE) != 0u;
    if (!a0 && !ube_n) return LANES_WORD;
    if (!a0 && ube_n) return LANE_LOW;
    if (a0 && !ube_n) return LANE_HIGH;
    return LANES_NONE;
}

static bool is_memory_read(uint32_t raw) {
    /* Minimum mode: IOM=1 selects memory, BUFRW=0 selects read, and active-low
     * INTAK must be deasserted so interrupt acknowledge is never a MEMR. */
    return sample_bit(raw, V30_PIN_IOM) != 0u &&
           sample_bit(raw, V30_PIN_BUFRW) == 0u &&
           sample_bit(raw, V30_PIN_INTAK) != 0u;
}

static bool is_memory_write(uint32_t raw) {
    /* Keep cycle-type predicates explicit. They are evidence classifiers and
     * document the exact control levels accepted by the physical tests. */
    return sample_bit(raw, V30_PIN_IOM) != 0u &&
           sample_bit(raw, V30_PIN_BUFRW) != 0u &&
           sample_bit(raw, V30_PIN_INTAK) != 0u;
}

static bool is_io_write(uint32_t raw) {
    /* IOM=0 distinguishes the I/O address space from physical memory. */
    return sample_bit(raw, V30_PIN_IOM) == 0u &&
           sample_bit(raw, V30_PIN_BUFRW) != 0u &&
           sample_bit(raw, V30_PIN_INTAK) != 0u;
}

static uint8_t decode_bus_byte(uint32_t raw, lane_mask_t lanes) {
    /* Odd ports such as 00E9h place their byte on AD15:AD8. */
    const uint16_t word = decode_ad(raw);
    return lanes == LANE_HIGH ? (uint8_t)(word >> 8) : (uint8_t)word;
}

static uint32_t encode_gpio_word(uint16_t value) {
    /* Expand a logical V30 word into the 28-bit PIO OUT window. Bits for
     * non-AD GPIOs stay zero; pin muxing and PINDIRS provide final ownership. */
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if ((value & (1u << bit)) != 0u)
            encoded |= 1u << ad_pins[bit];
    }
    return encoded;
}

static uint32_t encode_gpio_address(uint32_t address) {
    /* Build the exact raw early-T1 address component expected from the V30. */
    uint32_t encoded = encode_gpio_word((uint16_t)address);
    if (address & (1u << 16)) encoded |= 1u << V30_PIN_A16;
    if (address & (1u << 17)) encoded |= 1u << V30_PIN_A17;
    if (address & (1u << 18)) encoded |= 1u << V30_PIN_A18;
    if (address & (1u << 19)) encoded |= 1u << V30_PIN_A19;
    return encoded;
}

static uint32_t encoded_drive_command(uint16_t value) {
    /* Bit 28 is consumed after OUT PINS,28. A clear bit is the explicit
     * no-drive sentinel used for qualified observation without AD ownership. */
    return encode_gpio_word(value) | (1u << RESPONSE_VALID_BIT);
}

static uint64_t timeout_us_from_clocks(uint32_t clocks) {
    /* Round up so host-side polling never expires before the requested number
     * of configured V30 clocks, then add a small scheduling margin. */
    return ((uint64_t)clocks * 1000000ull + PC1C0C_V30_HZ - 1u) /
           PC1C0C_V30_HZ + 2u;
}

static void prepare_header_high_z(void) {
    /* Establish a fail-safe baseline before any PIO program is loaded. The
     * complete legacy GPIO0-27 data plane begins as input with no pulls. */
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
    /* Function selection and OE are both normalized: merely clearing PIO
     * PINDIRS is insufficient evidence once ownership returns to firmware. */
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

static void route_ad_to_responder(const pc1c_sm_t *responder) {
    /* PIO1 receives pin-mux ownership here, but the responder's PINDIRS remain
     * input until a valid descriptor reaches the qualified data phase. */
    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(responder->pio, ad_pins[bit]);
}

static void clock_init(pc1c_sm_t *clock) {
    clock->pio = pio1;
    clock->sm = pio_claim_unused_sm(clock->pio, true);
    clock->offset = pio_add_program(clock->pio, &perf_continuous_clk_program);
}

static void clock_prepare(pc1c_sm_t *clock) {
    /* The two-instruction PIO program toggles CLK once per instruction, hence
     * sys_clk / (2 * requested V30 clock). Preparation always parks CLK low. */
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
    pio_sm_config c = perf_continuous_clk_program_get_default_config(clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)PC1C0C_V30_HZ));
    pio_gpio_init(clock->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm,
                                  V30_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(clock->pio, clock->sm, clock->offset, &c) == PICO_OK);
    pio_sm_set_pins_with_mask(clock->pio, clock->sm, 0u, 1u << V30_PIN_CLK);
}

static void clock_start(pc1c_sm_t *clock) {
    clock_prepare(clock);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
}

static void clock_stop_low(pc1c_sm_t *clock) {
    /* Reassert deterministic terminal ownership through SIO. Do not leave the
     * external CPU clock at an unknown PIO phase after RESET is asserted. */
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static bool wait_reset_clocks(uint count) {
    /* Qualify complete high/low clock pairs while every response/observer SM
     * is disabled. This creates the clean RESET-only epoch used by all PC1-C
     * tests and catches a missing or stuck clock before bus ownership changes. */
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

static void matcher_init(pc1c_sm_t *matcher) {
    /* IN base 0 makes MOV ISR,PINS capture the full GPIO bank without shifting
     * the scattered address/control signals into a synthetic layout. */
    matcher->pio = pio1;
    matcher->sm = pio_claim_unused_sm(matcher->pio, true);
    matcher->offset = pio_add_program(matcher->pio,
                                      &pc1c_pio_rom_matcher_program);
    pio_sm_config c =
        pc1c_pio_rom_matcher_program_get_default_config(matcher->offset);
    sm_config_set_in_pins(&c, 0u);
    hard_assert(pio_sm_init(matcher->pio, matcher->sm,
                            matcher->offset, &c) == PICO_OK);
    pio_sm_set_enabled(matcher->pio, matcher->sm, false);
}

static void responder_init(pc1c_sm_t *responder) {
    /* Right shift is part of the descriptor ABI: OUT PINS consumes bits 0..27,
     * then OUT X consumes response-valid bit 28. */
    responder->pio = pio1;
    responder->sm = pio_claim_unused_sm(responder->pio, true);
    responder->offset = pio_add_program(
        responder->pio, &pc1c_pio_rom_responder_program);
    pio_sm_config c = pc1c_pio_rom_responder_program_get_default_config(
        responder->offset);
    sm_config_set_out_pins(&c, OUT_BASE, OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    hard_assert(pio_sm_init(responder->pio, responder->sm,
                            responder->offset, &c) == PICO_OK);
    pio_sm_set_enabled(responder->pio, responder->sm, false);
}

static void phase_capture_init(pc1c_sm_t *phase) {
    /* One-shot first-cycle R1/F1/R2/F2/R3 evidence is intentionally separate
     * from the continuous observer so the response window has its own gate. */
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

static void observer_init(pc1c_sm_t *observer) {
    /* Joined RX FIFO plus paced DMA provides room for paired T1/R2 snapshots
     * without putting the M33 in the observation path. */
    observer->pio = pio0;
    observer->sm = pio_claim_unused_sm(observer->pio, true);
    observer->offset = pio_add_program(
        observer->pio, &pc1c_sram_rom_execution_observer_program);
    pio_sm_config c =
        pc1c_sram_rom_execution_observer_program_get_default_config(
            observer->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(observer->pio, observer->sm,
                            observer->offset, &c) == PICO_OK);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
}

static void arm_sm(pc1c_sm_t *sm) {
    /* Restart clears execution state but does not imply a known PC; executing
     * JMP offset explicitly parks every SM at its program entry while disabled. */
    pio_sm_set_enabled(sm->pio, sm->sm, false);
    pio_sm_clear_fifos(sm->pio, sm->sm);
    pio_sm_restart(sm->pio, sm->sm);
    pio_sm_exec(sm->pio, sm->sm, pio_encode_jmp(sm->offset));
}

static void responder_preserve_clock_direction(pc1c_sm_t *responder) {
    /* The responder releases AD with MOV PINDIRS,Y. Y must therefore contain
     * only the CLK direction bit: CLK remains an output, every AD pin returns
     * to input. Loading Y via injected instructions avoids consuming a normal
     * response descriptor from the DMA-fed FIFO. */
    pio_sm_put_blocking(responder->pio, responder->sm,
                        1u << (V30_PIN_CLK - OUT_BASE));
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_pull(false, false));
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_mov(pio_y, pio_osr));
}

static int start_pio_tx_dma(const pc1c_sm_t *sm, const uint32_t *table,
                            uint32_t count) {
    /* One 32-bit SRAM word per PIO TX request. The peripheral address is fixed;
     * only the immutable table pointer advances. High priority reduces refill
     * jitter but correctness still requires the pre-release FIFO gate. */
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(sm->pio, sm->sm, true));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c, &sm->pio->txf[sm->sm], table,
                          count, true);
    return channel;
}

static int start_observer_dma(const pc1c_sm_t *observer) {
    /* Mirror of the TX path: fixed PIO RX FIFO source, incrementing SRAM
     * destination, and RX DREQ pacing. The buffer stores raw evidence only. */
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c,
                            pio_get_dreq(observer->pio, observer->sm, false));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c, g_observer_dma_words,
                          &observer->pio->rxf[observer->sm],
                          OBSERVER_WORDS, true);
    return channel;
}

static uint32_t dma_remaining(int channel) {
    /* RP2350 transfer_count exposes mode bits above the live 28-bit count. */
    return dma_channel_hw_addr((uint)channel)->transfer_count & 0x0FFFFFFFu;
}

static bool wait_fifo_primed(const pc1c_sm_t *sm, uint32_t level) {
    /* Four words fill a normal PIO TX FIFO. RESET must remain asserted until
     * matcher and responder both have an equal, immediately available prefix. */
    const uint64_t deadline = time_us_64() + 10000u;
    while (time_us_64() <= deadline) {
        if (pio_sm_get_tx_fifo_level(sm->pio, sm->sm) >= level) return true;
        tight_loop_contents();
    }
    return false;
}

static bool lookup_rom_word(uint32_t address, uint16_t *word) {
    /* This lookup runs only before RESET release and while classifying retained
     * evidence after the run. It is never a current-cycle response service. */
    static const uint8_t reset_rom[RESET_ROM_SIZE] = {
        0xEAu, 0x00u, 0x00u, 0x00u, 0xF0u, 0x90u,
    };
    const uint8_t *bytes = NULL;
    size_t size = 0u;
    uint32_t offset = 0u;
    if (address >= RESET_ROM_BASE && address < RESET_ROM_BASE + RESET_ROM_SIZE) {
        bytes = reset_rom;
        size = sizeof(reset_rom);
        offset = address - RESET_ROM_BASE;
    } else if (address >= V30_ROM_BASE &&
               address < V30_ROM_BASE + PC1C_ROM_SIZE) {
        bytes = PC1C_ROM_DATA;
        size = PC1C_ROM_SIZE;
        offset = address - V30_ROM_BASE;
    } else {
        return false;
    }
    /* C0C0 descriptors serve aligned word reads. The final byte of an odd-sized
     * image would be zero-extended, although generated test ROMs are padded. */
    if ((offset & 1u) != 0u || offset >= size) return false;
    *word = bytes[offset];
    if (offset + 1u < size) *word |= (uint16_t)bytes[offset + 1u] << 8;
    return true;
}

static void append_response(uint32_t address, uint16_t word) {
    /* Array indices are the pairing contract consumed by the two independent
     * DMA channels. The key is an exact early-T1 raw bank; the response is a
     * scattered-GPIO bitmap plus bit-28 drive authorization. */
    hard_assert(g_sequence_count < SEQUENCE_MAX);
    g_sequence_keys[g_sequence_count] = QUALIFIED_T1_CONTROL_BITS |
        encode_gpio_address(address);
    g_sequence_responses[g_sequence_count] = encoded_drive_command(word);
    ++g_sequence_count;
}

static void prepare_response_tables(void) {
    /* Compile the single known execution path before any physical activity:
     * reset-vector words, every sequential program word, then extra copies of
     * the final JMP $ word so repeated checkpoint fetches are measurable. */
    g_sequence_count = 0u;
    for (uint32_t address = RESET_ROM_BASE;
         address < RESET_ROM_BASE + RESET_ROM_SIZE; address += 2u) {
        uint16_t word = 0u;
        hard_assert(lookup_rom_word(address, &word));
        append_response(address, word);
    }
    for (uint32_t offset = 0u; offset < PC1C_ROM_SIZE; offset += 2u) {
        uint16_t word = 0u;
        hard_assert(lookup_rom_word(V30_ROM_BASE + offset, &word));
        append_response(V30_ROM_BASE + offset, word);
    }
    hard_assert(PC1C_ROM_SIZE >= 2u);
    g_checkpoint_address = V30_ROM_BASE +
                           (uint32_t)PC1C_ROM_SIZE - 2u;
    uint16_t checkpoint_word = 0u;
    hard_assert(lookup_rom_word(g_checkpoint_address, &checkpoint_word));
    for (uint i = 1u; i < CHECKPOINT_RESPONSES; ++i)
        append_response(g_checkpoint_address, checkpoint_word);
}

static void stop_dma(int channel) {
    /* Abort is harmless after normal completion and also bounds cleanup after
     * an invalid epoch. Unclaim only after every post-run counter is sampled. */
    dma_channel_abort((uint)channel);
    dma_channel_unclaim((uint)channel);
}

static void classify_trace(pc1c0c_result_t *result) {
    /* Observer DMA framing is [T1 address/control, R2 data] per complete cycle.
     * Classification happens only after RESET is reasserted and cannot affect
     * what the V30 consumed. */
    result->trace_count = result->observer_words / 2u;
    if (result->trace_count > OBSERVER_CYCLES)
        result->trace_count = OBSERVER_CYCLES;
    for (uint i = 0u; i < result->trace_count; ++i) {
        bus_trace_t *entry = &result->trace[i];
        entry->address_raw = g_observer_dma_words[i * 2u];
        entry->data_raw = g_observer_dma_words[i * 2u + 1u];
        entry->lanes = decode_lanes(entry->address_raw);
        entry->memory_read = is_memory_read(entry->address_raw);
        entry->memory_write = is_memory_write(entry->address_raw);
        entry->io_write = is_io_write(entry->address_raw);
        const uint32_t address = decode_address(entry->address_raw);
        entry->rom_hit = entry->memory_read && entry->lanes == LANES_WORD &&
            lookup_rom_word(address, &entry->response);
        if (entry->rom_hit) {
            /* Comparing retained R2 data with ROM content is the physical
             * response-deadline discriminator, not a software completion bit. */
            ++result->rom_hits;
            if (decode_ad(entry->data_raw) != entry->response)
                ++result->deadline_misses;
        } else {
            ++result->unsupported_cycles;
        }
        if (entry->memory_read && address == V30_ROM_BASE)
            result->far_target_seen = true;
        if (entry->memory_read && address == g_checkpoint_address)
            ++result->checkpoint_reads;
        if (entry->memory_write && entry->lanes == LANES_WORD) {
            /* The signature ROM exposes CPU progress through qualified writes;
             * no RAM backend is implied by observing these payloads. */
            const uint16_t data = decode_ad(entry->data_raw);
            if (address == 0xF0100u && data == 0x1234u)
                result->signature_1234 = true;
            if (address == 0xF0102u && data == 0x5678u)
                result->signature_5678 = true;
            if (address == 0xF0104u && data == 0xABCDu)
                result->signature_abcd = true;
        }
        if (entry->io_write &&
            (entry->lanes == LANE_LOW || entry->lanes == LANE_HIGH) &&
            decode_ad(entry->address_raw) == DIAGNOSTIC_PORT &&
            result->diagnostic_bytes < DIAGNOSTIC_MAX_BYTES) {
            /* 00E9h is odd, so the validated HELLO path normally selects the
             * high lane. Keep low-lane decoding generic for future consoles. */
            result->diagnostic[result->diagnostic_bytes++] =
                (char)decode_bus_byte(entry->data_raw, entry->lanes);
        }
    }
    result->diagnostic[result->diagnostic_bytes] = '\0';
#ifdef PC1C_DIAGNOSTIC_BUILD
    result->diagnostic_ok =
        result->diagnostic_bytes == sizeof(expected_diagnostic) - 1u &&
        memcmp(result->diagnostic, expected_diagnostic,
               sizeof(expected_diagnostic) - 1u) == 0;
#endif
    result->checkpoint_ok = result->checkpoint_reads >= CHECKPOINT_RESPONSES;
}

static void run_test(pc1c_sm_t *clock, pc1c_sm_t *matcher,
                     pc1c_sm_t *phase, pc1c_sm_t *observer,
                     pc1c_sm_t *responder, pc1c0c_result_t *result) {
    /* Phase 0 - force a known safe baseline before generating RESET clocks. */
    *result = (pc1c0c_result_t){0};
    for (uint i = 0u; i < OBSERVER_WORDS; ++i) g_observer_dma_words[i] = 0u;
    gpio_put(V30_PIN_INTR, false);
    gpio_put(V30_PIN_RESET, true);
    route_ad_to_sio_high_z();

    /* Phase 1 - clock-only RESET qualification. No matcher, responder, phase
     * capture, or observer SM is enabled in this interval. */
    clock_start(clock);
    result->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    clock_stop_low(clock);

    /* Phase 2 - with RESET still asserted and CLK stopped low, restart every
     * SM, install the responder's CLK-only release mask, start DMA, and fill
     * both response FIFOs. No CPU-visible bus cycle is allowed yet. */
    clock_prepare(clock);
    arm_sm(matcher);
    arm_sm(phase);
    arm_sm(observer);
    arm_sm(responder);
    pio_interrupt_clear(pio1, 0u);
    responder_preserve_clock_direction(responder);
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_mov(pio_pindirs, pio_y));

    const int matcher_dma = start_pio_tx_dma(
        matcher, g_sequence_keys, g_sequence_count);
    const int responder_dma = start_pio_tx_dma(
        responder, g_sequence_responses, g_sequence_count);
    const bool matcher_primed = wait_fifo_primed(matcher, 4u);
    const bool responder_primed = wait_fifo_primed(responder, 4u);
    result->matcher_fifo_pre = pio_sm_get_tx_fifo_level(matcher->pio,
                                                         matcher->sm);
    result->responder_fifo_pre = pio_sm_get_tx_fifo_level(responder->pio,
                                                           responder->sm);
    result->matcher_dma_pre = dma_remaining(matcher_dma);
    result->responder_dma_pre = dma_remaining(responder_dma);

    /* Pin mux may now point AD at PIO1, but PINDIRS must still prove CLK-only.
     * This separates functional ownership from electrical output enable. */
    route_ad_to_responder(responder);
    result->pre_pio1_padoe = pio1->dbg_padoe;
    result->clock_direction_armed =
        (result->pre_pio1_padoe & (1u << V30_PIN_CLK)) != 0u &&
        (result->pre_pio1_padoe & V30_AD_BUS_MASK) == 0u;
    const int observer_dma = start_observer_dma(observer);
    result->observer_dma_pre = dma_remaining(observer_dma);
    result->pre_release_clean = matcher_primed && responder_primed &&
        pio_sm_is_rx_fifo_empty(matcher->pio, matcher->sm) &&
        pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
        pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
        result->matcher_fifo_pre == 4u && result->responder_fifo_pre == 4u &&
        result->observer_dma_pre == OBSERVER_WORDS &&
        result->clock_direction_armed && !gpio_get(V30_PIN_CLK) &&
        (sio_hw->gpio_oe & V30_AD_BUS_MASK) == 0u;

    /* Passive instruments arm first. RESET is still high, so any RX event here
     * would invalidate the clean measurement epoch. */
    pio_enable_sm_mask_in_sync(pio0,
        (1u << phase->sm) | (1u << observer->sm));

    if (result->reset_ok && result->pre_release_clean) {
        /* Phase 3 - release RESET and start clock/matcher/responder on one PIO1
         * cycle boundary. Mask M33 interrupts so host-side service cannot add
         * nondeterministic latency around the bounded physical run. */
        const uint32_t irq_state = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_enable_sm_mask_in_sync(pio1,
            (1u << clock->sm) | (1u << matcher->sm) |
            (1u << responder->sm));
        const uint64_t deadline =
            time_us_64() + timeout_us_from_clocks(RUN_TIMEOUT_CLOCKS);
        /* Response-stream exhaustion is the deterministic stop trigger. The
         * matcher can ignore unrelated cycles indefinitely without consuming
         * the final response, so the outer timeout still bounds failure. */
        while ((dma_remaining(responder_dma) != 0u ||
                !pio_sm_is_tx_fifo_empty(responder->pio, responder->sm)) &&
               time_us_64() <= deadline)
            tight_loop_contents();
        if (dma_remaining(responder_dma) == 0u &&
            pio_sm_is_tx_fifo_empty(responder->pio, responder->sm))
            busy_wait_us_32((uint32_t)timeout_us_from_clocks(2u));
        /* Reassert RESET before stopping CLK; the terminal contract requires
         * the external CPU halted before any responder/observer teardown. */
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
        restore_interrupts(irq_state);
    } else {
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
    }

    /* Phase 4 - sample transport state before disabling or aborting anything.
     * A response word is complete only when it is absent from both DMA and TX
     * FIFO, hence the remaining-count calculation below. */
    result->matcher_dma_post = dma_remaining(matcher_dma);
    result->responder_dma_post = dma_remaining(responder_dma);
    result->matcher_fifo_post = pio_sm_get_tx_fifo_level(matcher->pio,
                                                          matcher->sm);
    result->responder_fifo_post = pio_sm_get_tx_fifo_level(responder->pio,
                                                            responder->sm);
    const uint32_t response_words_remaining = result->responder_dma_post +
                                               result->responder_fifo_post;
    result->qualified_pairs = response_words_remaining <= g_sequence_count ?
        g_sequence_count - response_words_remaining : 0u;
    result->dma_streams_complete = result->matcher_dma_post == 0u &&
        result->responder_dma_post == 0u;

    /* Disable all non-clock SMs, then explicitly force responder PINDIRS to
     * zero. SIO high-Z normalization occurs after evidence extraction. */
    pio_sm_set_enabled(matcher->pio, matcher->sm, false);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
    pio_sm_set_enabled(responder->pio, responder->sm, false);
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));

    /* The first-cycle instrument is one-shot and yields at most six words. */
    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           result->phase_count < FIRST_PHASE_COUNT) {
        result->phase_raw[result->phase_count++] =
            pio_sm_get(phase->pio, phase->sm);
    }
    for (uint spin = 0u; spin < 4096u &&
         !pio_sm_is_rx_fifo_empty(observer->pio, observer->sm); ++spin)
        tight_loop_contents();
    result->observer_fifo_residue =
        pio_sm_get_rx_fifo_level(observer->pio, observer->sm);
    result->observer_dma_post = dma_remaining(observer_dma);
    result->observer_words = OBSERVER_WORDS - result->observer_dma_post;
    /* RESET may truncate the next speculative cycle after its early-T1 push.
     * Accept one odd tail only when that retained word still has ASTB high;
     * never count it as a complete T1/R2 pair. */
    result->observer_trailing_words = result->observer_words & 1u;
    result->observer_tail_valid = result->observer_trailing_words == 0u ||
        sample_bit(g_observer_dma_words[result->observer_words - 1u],
                   V30_PIN_ASTB) != 0u;

    result->dma_observer_first_ok = result->observer_words >= 2u &&
        decode_address(g_observer_dma_words[0]) == RESET_ROM_BASE;
    result->first_address_ok = result->dma_observer_first_ok;
    result->first_memory_read = result->observer_words >= 2u &&
        is_memory_read(g_observer_dma_words[0]);
    if (result->phase_count == FIRST_PHASE_COUNT) {
        result->first_response_phase_ok =
            decode_ad(result->phase_raw[3]) == 0x00EAu &&
            decode_ad(result->phase_raw[4]) == 0x00EAu &&
            decode_ad(result->phase_raw[5]) == 0x00EAu;
    }
    classify_trace(result);

    /* Phase 5 - DMA teardown and final electrical normalization. */
    stop_dma(matcher_dma);
    stop_dma(responder_dma);
    stop_dma(observer_dma);
    route_ad_to_sio_high_z();
    gpio_put(V30_PIN_INTR, false);
    result->terminal_safe = gpio_get(V30_PIN_RESET) &&
                            !gpio_get(V30_PIN_CLK) && ad_is_sio_high_z();
}

static bool result_valid(const pc1c0c_result_t *result) {
    /* VALID means the measurement epoch and evidence framing are trustworthy.
     * It is intentionally distinct from the CPU-visible functional PASS. */
    return result->reset_ok && result->pre_release_clean &&
           result->first_address_ok && result->first_memory_read &&
           result->dma_observer_first_ok &&
           result->observer_tail_valid &&
           result->observer_fifo_residue == 0u &&
           result->phase_count == FIRST_PHASE_COUNT;
}

static bool result_pass(const pc1c0c_result_t *result) {
    /* Each build selects its own CPU-visible payload discriminator while all
     * timing, transport, ownership, and terminal-safety gates remain shared. */
    const bool payload_ok =
#ifdef PC1C_DIAGNOSTIC_BUILD
        result->diagnostic_ok;
#else
        result->signature_1234 && result->signature_5678 &&
        result->signature_abcd;
#endif
    return result_valid(result) && result->first_response_phase_ok &&
           result->far_target_seen && payload_ok &&
           result->checkpoint_ok && result->deadline_misses == 0u &&
           result->unqualified_drive_commands == 0u &&
           result->dma_streams_complete &&
           result->qualified_pairs == g_sequence_count &&
           result->matcher_fifo_post == 0u &&
           result->responder_fifo_post == 0u && result->terminal_safe;
}

static const char *lane_name(lane_mask_t lanes) {
    switch (lanes) {
        case LANE_LOW: return "LOW";
        case LANE_HIGH: return "HIGH";
        case LANES_WORD: return "WORD";
        default: return "NONE";
    }
}

static const char *cycle_name(const bus_trace_t *entry) {
    if (entry->memory_read) return "MEMR";
    if (entry->memory_write) return "MEMW";
    if (entry->io_write) return "IOW";
    return "OTHER";
}

static const char *pass_fail(bool pass) {
    return pass ? "PASS" : "FAIL";
}

static void print_readable_summary(const pc1c0c_result_t *result) {
    const bool reset_vector_ok = result->reset_ok &&
        result->first_address_ok && result->first_memory_read &&
        result->first_response_phase_ok;
    const bool rom_execution_ok = result->far_target_seen &&
        result->checkpoint_ok;
    const bool response_path_ok = result->deadline_misses == 0u &&
        result->dma_streams_complete &&
        result->qualified_pairs == g_sequence_count;
    const bool bus_safety_ok = result->unqualified_drive_commands == 0u &&
        result->terminal_safe;

#ifdef PC1C_DIAGNOSTIC_BUILD
    printf("\n[V30 BIOS OUTPUT]\n");
    if (result->diagnostic_bytes == 0u) {
        printf("(no diagnostic output)\n");
    } else {
        fwrite(result->diagnostic, 1u, result->diagnostic_bytes, stdout);
        if (result->diagnostic[result->diagnostic_bytes - 1u] != '\n')
            putchar('\n');
    }
#endif

    printf("\n[SUMMARY]\n");
    printf("Measurement epoch    %s\n", pass_fail(result_valid(result)));
    printf("Reset / FFFF0 fetch  %s\n", pass_fail(reset_vector_ok));
    printf("F0000 ROM execution  %s\n", pass_fail(rom_execution_ok));
#ifdef PC1C_DIAGNOSTIC_BUILD
    printf("Diagnostic I/O 00E9  %s\n", pass_fail(result->diagnostic_ok));
#else
    printf("Memory signatures    %s\n",
           pass_fail(result->signature_1234 && result->signature_5678 &&
                     result->signature_abcd));
#endif
    printf("Response path        %s\n", pass_fail(response_path_ok));
    printf("Bus ownership/safety %s\n", pass_fail(bus_safety_ok));
    printf("Overall result       %s\n", pass_fail(result_pass(result)));
}

static void print_result(const pc1c0c_result_t *result) {
    /* USB reporting is strictly post-run. None of these formatting paths can
     * perturb a V30 bus deadline or mask the raw physical evidence. */
    print_readable_summary(result);
    printf("\n[ENGINEERING DETAILS]\n");
#ifdef PC1C_DIAGNOSTIC_BUILD
    printf("\n%s\n", PC1C_RUN_TITLE);
#else
    printf("\nPC1-C0C0 Descriptor-Fed SRAM ROM Execution - 0.300 MHz\n");
#endif
    printf("RESET qualification : clock-only; matcher/responder SMs disabled\n");
    printf("Measurement epoch   : arm after RESET clocks with CLK stopped LOW\n");
    printf("Realtime engine     : PIO1 synchronized CLK + exact matcher + AD/PINDIRS\n");
    printf("Response policy     : SRAM key/descriptor tables -> DMA -> PIO1 FIFOs\n");
    printf("Current-cycle M33   : NONE\n");
    printf("Observer path       : passive PIO0 address/R2-data -> DMA -> SRAM\n");
    printf("ROM image           : %lu bytes at F0000; SHA-256 %s\n",
           (unsigned long)PC1C_ROM_SIZE, PC1C_ROM_SHA256);
    printf("Input synchronizers : SDK defaults\n\n");
    printf("RESET clock count         = %s\n", result->reset_ok ? "PASS" : "FAIL");
    printf("PRE-RESET EVENT LEAK      = %s\n",
           result->pre_release_clean ? "NO" : "YES / INVALID");
    printf("PIO1 pre-release OE       = %08lX %s\n",
           (unsigned long)result->pre_pio1_padoe,
           result->clock_direction_armed ? "CLK-ONLY PASS" : "FAIL");
    printf("FIRST post-reset address  = %s\n",
           result->first_address_ok ? "FFFF0 PASS" : "FAIL");
    printf("FIRST cycle type          = %s\n",
           result->first_memory_read ? "MEMORY READ PASS" : "FAIL");
    printf("Matcher FIFO primed       = %lu/4 %s\n",
           (unsigned long)result->matcher_fifo_pre,
           result->matcher_fifo_pre == 4u ? "PASS" : "FAIL");
    printf("Responder FIFO primed     = %lu/4 %s\n",
           (unsigned long)result->responder_fifo_pre,
           result->responder_fifo_pre == 4u ? "PASS" : "FAIL");
    printf("PRE-RELEASE DMA remain    = key %lu response %lu\n",
           (unsigned long)result->matcher_dma_pre,
           (unsigned long)result->responder_dma_pre);
    printf("DMA observer first address= %s\n",
           result->dma_observer_first_ok ? "FFFF0 PASS" : "FAIL");
    printf("FIRST response R2/F2/R3   = %s\n",
           result->first_response_phase_ok ? "00EA PASS" : "FAIL");
    printf("Far-jump target observed  = %s\n",
           result->far_target_seen ? "F0000 PASS" : "FAIL");
#ifdef PC1C_DIAGNOSTIC_BUILD
    printf("Diagnostic port 00E9 bytes= %lu/%lu %s\n",
           (unsigned long)result->diagnostic_bytes,
           (unsigned long)(sizeof(expected_diagnostic) - 1u),
           result->diagnostic_ok ? "PASS" : "FAIL");
    printf("V30 diagnostic output     = \"");
    for (uint i = 0u; i < result->diagnostic_bytes; ++i) {
        const char ch = result->diagnostic[i];
        if (ch == '\r') printf("\\r");
        else if (ch == '\n') printf("\\n");
        else if (ch >= 32 && ch <= 126) putchar(ch);
        else printf("\\x%02X", (unsigned)(uint8_t)ch);
    }
    printf("\"\n");
#else
    printf("Write F0100=1234          = %s\n",
           result->signature_1234 ? "PASS" : "FAIL");
    printf("Write F0102=5678          = %s\n",
           result->signature_5678 ? "PASS" : "FAIL");
    printf("Write F0104=ABCD          = %s\n",
           result->signature_abcd ? "PASS" : "FAIL");
#endif
    printf("Checkpoint %05lX reads   = %lu/%u %s\n",
           (unsigned long)g_checkpoint_address,
           (unsigned long)result->checkpoint_reads, CHECKPOINT_RESPONSES,
           result->checkpoint_ok ? "PASS" : "FAIL");
    printf("PIO-qualified pairs       = %lu/%lu %s\n",
           (unsigned long)result->qualified_pairs,
           (unsigned long)g_sequence_count,
           result->qualified_pairs == g_sequence_count ? "PASS" : "FAIL");
    printf("POST-RUN DMA remain       = key %lu response %lu %s\n",
           (unsigned long)result->matcher_dma_post,
           (unsigned long)result->responder_dma_post,
           result->dma_streams_complete ? "PASS" : "FAIL");
    printf("Matcher FIFO remain       = %lu %s\n",
           (unsigned long)result->matcher_fifo_post,
           result->matcher_fifo_post == 0u ? "PASS" : "FAIL");
    printf("Responder FIFO remain     = %lu %s\n",
           (unsigned long)result->responder_fifo_post,
           result->responder_fifo_post == 0u ? "PASS" : "FAIL");
    printf("Observer complete cycles  = %u\n", result->trace_count);
    printf("Observer terminal T1 tail = %lu %s\n",
           (unsigned long)result->observer_trailing_words,
           result->observer_tail_valid ? "VALID" : "INVALID");
    printf("Observer FIFO residue     = %lu %s\n",
           (unsigned long)result->observer_fifo_residue,
           result->observer_fifo_residue == 0u ? "PASS" : "FAIL");
    printf("Response deadline misses  = %lu %s\n",
           (unsigned long)result->deadline_misses,
           result->deadline_misses == 0u ? "PASS" : "FAIL");
    printf("Unqualified drive commands= %lu %s\n",
           (unsigned long)result->unqualified_drive_commands,
           result->unqualified_drive_commands == 0u ? "PASS" : "FAIL");

    /* Print a bounded prefix for CDC readability; the complete raw capture
     * remains in SRAM for classification and retained-cycle accounting. */
    printf("\n[PASSIVE ADDRESS / R2-DATA TRACE]\n");
    printf("idx address type lanes addr_raw data_raw data hit\n");
    const uint print_count = result->trace_count < OBSERVER_PRINT_DEPTH ?
        result->trace_count : OBSERVER_PRINT_DEPTH;
    for (uint i = 0u; i < print_count; ++i) {
        const bus_trace_t *entry = &result->trace[i];
        printf("%02u  %05lX  %-5s %-4s %08lX %08lX %04X %s\n",
               i, (unsigned long)decode_address(entry->address_raw),
               cycle_name(entry), lane_name(entry->lanes),
               (unsigned long)entry->address_raw,
               (unsigned long)entry->data_raw, decode_ad(entry->data_raw),
               entry->rom_hit ? "YES" : "NO");
    }
    if (result->trace_count > print_count)
        printf("... %u additional cycles retained in SRAM\n",
               result->trace_count - print_count);

    static const char *const phase_names[FIRST_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    printf("\n[FIRST-CYCLE GPIO SNAPSHOTS]\n");
    printf("phase raw_gpio  ASTB CLK IOM BUFRW INTAK UBE AD16\n");
    for (uint i = 0u; i < result->phase_count; ++i) {
        const uint32_t raw = result->phase_raw[i];
        printf("%-4s  %08lX   %u   %u   %u   %u    %u   %u  %04X\n",
               phase_names[i], (unsigned long)raw,
               (unsigned)sample_bit(raw, V30_PIN_ASTB),
               (unsigned)sample_bit(raw, V30_PIN_CLK),
               (unsigned)sample_bit(raw, V30_PIN_IOM),
               (unsigned)sample_bit(raw, V30_PIN_BUFRW),
               (unsigned)sample_bit(raw, V30_PIN_INTAK),
               (unsigned)sample_bit(raw, V30_PIN_UBE), decode_ad(raw));
    }

    printf("\nMEASUREMENT EPOCH   = %s\n",
           result_valid(result) ? "VALID" : "INVALID");
#ifdef PC1C_DIAGNOSTIC_BUILD
    printf("PC1-C0C0-H RESULT   = %s\n",
#else
    printf("PC1-C0C0 RESULT     = %s\n",
#endif
           result_pass(result) ? "PASS" :
           (result_valid(result) ? "FAIL" : "INVALID"));
    printf("TERMINAL SAFE STATE = %s\n",
           result->terminal_safe ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    /* Generate every known-path descriptor before touching the external bus. */
    prepare_response_tables();
    prepare_header_high_z();
    init_control_outputs();
    route_ad_to_sio_high_z();
    /* RESET is already asserted and AD is high-Z while waiting for a host.
     * Waiting here guarantees the one-shot validation report is not lost to
     * USB enumeration; USB is never serviced during the timed run itself. */
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    pc1c_sm_t clock, matcher, phase, observer, responder;
    clock_init(&clock);
    matcher_init(&matcher);
    phase_capture_init(&phase);
    observer_init(&observer);
    responder_init(&responder);

    /* Static storage keeps the large observer trace off the M33 stack. */
    static pc1c0c_result_t result;
    run_test(&clock, &matcher, &phase, &observer, &responder, &result);
    print_result(&result);
    fflush(stdout);
    while (true) tight_loop_contents();
}
