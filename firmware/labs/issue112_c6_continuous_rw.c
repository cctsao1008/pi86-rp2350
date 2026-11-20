/*
 * Issue #112 C6: 4 MHz continuous WORD read + physical WORD-write witness.
 *
 * This is the first post-C5 workload-oriented proof. It deliberately keeps
 * the 64 KiB mirrored, pre-encoded laboratory memory representation because
 * the immediate question is whether the already-proven C5 read latency can be
 * sustained across consecutive physical 8086 bus cycles.
 *
 * The CPU executes real 8086 code:
 *
 *   RESET FFFF0 -> FAR JMP 1000:0000
 *   1000:0000:
 *       CLI
 *       MOV AX,1000h
 *       MOV DS,AX
 *       MOV AX,1234h
 *       MOV [0800h],AX
 *       JMP $
 *
 * Acceptance:
 *   - continuous read service survives enough bus cycles to execute the code;
 *   - a physical WORD MEM_WRITE to 10800h is observed;
 *   - R2 contains 1234h on AD15..0;
 *   - no M33 callback services an active cycle.
 *
 * The write is observed but NOT committed to SRAM in C6. C7 will connect the
 * proven B3 dynamic write primitive to this captured write phase, after which
 * the PI-STRESS image can replace this synthetic program.
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
#include "issue112_c6_continuous_rw.pio.h"
#include "intel_8086_clock.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#define ISSUE112_PROCESSOR_HZ 4000000u
#define ISSUE112_TIMEOUT_US 250000u
#define ISSUE112_CYCLE_LIMIT 64u
#define ISSUE112_OBSERVER_WORDS (ISSUE112_CYCLE_LIMIT * 2u)
#define ISSUE112_ADDR_LUT_ENTRIES 16384u
#define ISSUE112_ENCODED_WORDS 32768u
#define ISSUE112_DMA_SNIFF_SUM_MODE 0x0fu

#define ISSUE112_EXPECTED_WRITE_ADDRESS 0x10800u
#define ISSUE112_EXPECTED_WRITE_DATA    0x1234u

static uint32_t addr_lut[2][ISSUE112_ADDR_LUT_ENTRIES]
    __attribute__((aligned(65536)));
static uint32_t encoded_backing[ISSUE112_ENCODED_WORDS];
static uint32_t address_high_part;
static uint32_t observer_words[ISSUE112_OBSERVER_WORDS];

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

static bool is_memory_write(uint32_t raw) {
    return sample_bit(raw, RP86_PROCESSOR_PIN_IOM) != 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) != 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_INTAK) != 0u;
}

static uint32_t encode_word(uint16_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        if ((value >> bit) & 1u) encoded |= 1u << ad_gpio[bit];
    return encoded;
}

static void set_encoded_word(uint16_t byte_address, uint16_t value) {
    hard_assert((byte_address & 1u) == 0u);
    encoded_backing[byte_address >> 1u] = encode_word(value);
}

static void load_bytes(uint16_t byte_address,
                       const uint8_t *bytes, size_t length) {
    hard_assert((byte_address & 1u) == 0u);
    hard_assert((length & 1u) == 0u);
    for (size_t i = 0u; i < length; i += 2u) {
        const uint16_t word =
            (uint16_t)bytes[i] | ((uint16_t)bytes[i + 1u] << 8u);
        set_encoded_word((uint16_t)(byte_address + i), word);
    }
}

static void prepare_memory(void) {
    const uintptr_t backing_base = (uintptr_t)encoded_backing;

    for (uint i = 0u; i < ISSUE112_ENCODED_WORDS; ++i)
        encoded_backing[i] = encode_word(0x9090u);

    /*
     * FFFF0: JMP FAR 1000:0000
     * Pad to an even number of bytes because the physical bus fetches words.
     */
    static const uint8_t reset_stub[] = {
        0xeau, 0x00u, 0x00u, 0x00u, 0x10u, 0x90u
    };
    load_bytes(0xfff0u, reset_stub, sizeof reset_stub);

    /*
     * 1000:0000, mirrored to low 0000h by this laboratory backing:
     *   cli
     *   mov ax,1000h
     *   mov ds,ax
     *   mov ax,1234h
     *   mov [0800h],ax
     *   jmp $
     */
    static const uint8_t program[] = {
        0xfau,
        0xb8u, 0x00u, 0x10u,
        0x8eu, 0xd8u,
        0xb8u, 0x34u, 0x12u,
        0xa3u, 0x00u, 0x08u,
        0xebu, 0xfeu
    };
    load_bytes(0x0000u, program, sizeof program);

    /*
     * Same speed-first C5 translation: the two 14-bit raw-GPIO LUTs contain
     * disjoint logical-address contributions. A logical even byte address A
     * selects encoded_backing[A >> 1], whose uint32_t byte offset is A << 1.
     */
    for (uint index = 0u; index < ISSUE112_ADDR_LUT_ENTRIES; ++index) {
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

    address_high_part = 0u;
    for (uint i = 0u; i < ISSUE112_OBSERVER_WORDS; ++i)
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
        issue112_c6_addr_frontend_program_get_default_config(offset);
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
        issue112_c6_responder_program_get_default_config(offset);
    sm_config_set_out_pins(&c, 0u, 28u);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_jmp_pin(&c, RP86_PROCESSOR_PIN_BUFRW);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);

    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(pio, ad_gpio[bit]);

    pio_sm_set_consecutive_pindirs(pio, sm, 0u, 28u, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void init_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue112_c6_observer_program_get_default_config(offset);
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
        (float)clock_get_hz(clk_sys) / (5.0f * (float)ISSUE112_PROCESSOR_HZ);
    sm_config_set_set_pins(&c, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c, divider);
    pio_gpio_init(pio, RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_set_enabled(pio, sm, true);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t deadline = time_us_64() + ISSUE112_TIMEOUT_US;
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

static const char *cycle_name(uint32_t raw) {
    if (sample_bit(raw, RP86_PROCESSOR_PIN_INTAK) == 0u) return "INTA";
    if (sample_bit(raw, RP86_PROCESSOR_PIN_IOM) != 0u)
        return sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) == 0u ?
            "MEMR" : "MEMW";
    return sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) == 0u ?
        "IOR" : "IOW";
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue112 C6 continuous 4MHz WORD R/W probe\n");
    printf("scope: continuous C5C reads -> execute native 8086 -> observe WORD write\n");
    printf("processor clock: %u Hz (engineering baseline; Intel compliance not claimed)\n",
           ISSUE112_PROCESSOR_HZ);

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
        pio_add_program(frontend_pio, &issue112_c6_addr_frontend_program);
    init_frontend(frontend_pio, frontend0_sm, frontend_offset, 0u, lut0_base);
    init_frontend(frontend_pio, frontend1_sm, frontend_offset, 14u, lut1_base);

    PIO io_pio = pio2;
    const uint responder_sm = pio_claim_unused_sm(io_pio, true);
    const uint observer_sm = pio_claim_unused_sm(io_pio, true);
    const uint responder_offset =
        pio_add_program(io_pio, &issue112_c6_responder_program);
    const uint observer_offset =
        pio_add_program(io_pio, &issue112_c6_observer_program);
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
    const uint pointer_dma = dma_claim_unused_channel(true);
    const uint raw_data_dma = dma_claim_unused_channel(true);
    const uint observer_dma = dma_claim_unused_channel(true);

    /*
     * C5C continuous form:
     * low_control is the only multi-transfer channel. Each frontend0 RX word
     * is written to low_lookup READ_ADDR_TRIG, which starts one transaction.
     */
    dma_channel_config low_lookup_cfg =
        dma_channel_get_default_config(low_lookup_dma);
    channel_config_set_transfer_data_size(&low_lookup_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&low_lookup_cfg, false);
    channel_config_set_write_increment(&low_lookup_cfg, false);
    channel_config_set_dreq(&low_lookup_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&low_lookup_cfg, high_control_dma);
    channel_config_set_high_priority(&low_lookup_cfg, true);
    dma_channel_configure(low_lookup_dma, &low_lookup_cfg,
                          &dma_hw->sniff_data, &addr_lut[0][0], 1u, false);

    dma_channel_config low_control_cfg =
        dma_channel_get_default_config(low_control_dma);
    channel_config_set_transfer_data_size(&low_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&low_control_cfg, false);
    channel_config_set_write_increment(&low_control_cfg, false);
    channel_config_set_dreq(
        &low_control_cfg,
        pio_get_dreq(frontend_pio, frontend0_sm, false));
    channel_config_set_high_priority(&low_control_cfg, true);
    dma_channel_configure(
        low_control_dma, &low_control_cfg,
        &dma_channel_hw_addr(low_lookup_dma)->al3_read_addr_trig,
        &frontend_pio->rxf[frontend0_sm], ISSUE112_CYCLE_LIMIT, false);

    dma_channel_config high_lookup_cfg =
        dma_channel_get_default_config(high_lookup_dma);
    channel_config_set_transfer_data_size(&high_lookup_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&high_lookup_cfg, false);
    channel_config_set_write_increment(&high_lookup_cfg, false);
    channel_config_set_dreq(&high_lookup_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&high_lookup_cfg, pointer_dma);
    channel_config_set_sniff_enable(&high_lookup_cfg, true);
    channel_config_set_high_priority(&high_lookup_cfg, true);
    dma_channel_configure(high_lookup_dma, &high_lookup_cfg,
                          &address_high_part, &addr_lut[1][0], 1u, false);

    dma_channel_config high_control_cfg =
        dma_channel_get_default_config(high_control_dma);
    channel_config_set_transfer_data_size(&high_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&high_control_cfg, false);
    channel_config_set_write_increment(&high_control_cfg, false);
    channel_config_set_dreq(
        &high_control_cfg,
        pio_get_dreq(frontend_pio, frontend1_sm, false));
    channel_config_set_high_priority(&high_control_cfg, true);
    dma_channel_configure(
        high_control_dma, &high_control_cfg,
        &dma_channel_hw_addr(high_lookup_dma)->al3_read_addr_trig,
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
        ISSUE112_OBSERVER_WORDS, false);

    dma_sniffer_set_data_accumulator(0u);
    dma_sniffer_enable(high_lookup_dma, ISSUE112_DMA_SNIFF_SUM_MODE, false);

    dma_channel_start(low_control_dma);
    dma_channel_start(observer_dma);

    pio_sm_restart(frontend_pio, frontend0_sm);
    pio_sm_exec(frontend_pio, frontend0_sm, pio_encode_jmp(frontend_offset));
    pio_sm_restart(frontend_pio, frontend1_sm);
    pio_sm_exec(frontend_pio, frontend1_sm, pio_encode_jmp(frontend_offset));
    pio_sm_restart(io_pio, responder_sm);
    pio_sm_exec(io_pio, responder_sm, pio_encode_jmp(responder_offset));
    pio_sm_restart(io_pio, observer_sm);
    pio_sm_exec(io_pio, observer_sm, pio_encode_jmp(observer_offset));

    pio_sm_set_enabled(frontend_pio, frontend0_sm, true);
    pio_sm_set_enabled(frontend_pio, frontend1_sm, true);
    pio_sm_set_enabled(io_pio, responder_sm, true);
    pio_sm_set_enabled(io_pio, observer_sm, true);

    init_clock(clock_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t observer_deadline = time_us_64() + ISSUE112_TIMEOUT_US;
    const bool observer_complete =
        wait_dma_idle(observer_dma, observer_deadline);
    const uint32_t observer_remaining =
        dma_channel_hw_addr(observer_dma)->transfer_count & 0x0fffffffu;

    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    sleep_us(100u);
    pio_sm_set_enabled(clock_pio, clock_sm, false);

    const uint64_t settle_deadline = time_us_64() + ISSUE112_TIMEOUT_US;
    const bool low_control_done =
        wait_dma_idle(low_control_dma, settle_deadline);
    const bool low_lookup_done =
        wait_dma_idle(low_lookup_dma, settle_deadline);
    const bool high_control_done =
        wait_dma_idle(high_control_dma, settle_deadline);
    const bool high_lookup_done =
        wait_dma_idle(high_lookup_dma, settle_deadline);
    const bool pointer_done =
        wait_dma_idle(pointer_dma, settle_deadline);
    const bool raw_done =
        wait_dma_idle(raw_data_dma, settle_deadline);

    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_gpio[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = RP86_PROCESSOR_AD_BUS_MASK;

    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint32_t captured_cycles =
        (ISSUE112_OBSERVER_WORDS - observer_remaining) / 2u;

    bool first_reset_vector_ok = false;
    bool entry_seen = false;
    bool write_seen = false;
    bool write_data_ok = false;
    uint32_t read_cycles = 0u;
    uint32_t write_cycles = 0u;

    for (uint i = 0u; i < captured_cycles; ++i) {
        const uint32_t t1 = observer_words[i * 2u];
        const uint32_t r2 = observer_words[i * 2u + 1u];
        const uint32_t address = decode_address(t1);
        const uint16_t data = decode_ad(r2);
        const bool word = is_word_lane(t1);
        const bool memr = is_memory_read(t1);
        const bool memw = is_memory_write(t1);

        if (i == 0u)
            first_reset_vector_ok = address == 0xffff0u && word && memr;
        if (address == 0x10000u && word && memr)
            entry_seen = true;
        if (memr) ++read_cycles;
        if (memw) {
            ++write_cycles;
            if (address == ISSUE112_EXPECTED_WRITE_ADDRESS && word) {
                write_seen = true;
                if (data == ISSUE112_EXPECTED_WRITE_DATA)
                    write_data_ok = true;
            }
        }

        printf("[%02u] %-4s addr=%05" PRIx32 " lane=%s r2=%04x%s\n",
               i, cycle_name(t1), address,
               word ? "WORD" : "OTHER", data,
               address == ISSUE112_EXPECTED_WRITE_ADDRESS ? " <WRITE-TARGET>" : "");
    }

    const bool pipeline_done =
        low_control_done && low_lookup_done && high_control_done &&
        high_lookup_done && pointer_done && raw_done;

    const bool pass =
        reset_clock_ok && observer_complete && first_reset_vector_ok &&
        entry_seen && write_seen && write_data_ok && pipeline_done;

    printf("ADDR LUT0 BASE: %08" PRIxPTR "\n", lut0_base);
    printf("ADDR LUT1 BASE: %08" PRIxPTR "\n", lut1_base);
    printf("ENCODED BACKING: %08" PRIxPTR "\n", backing_base);
    printf("OBSERVER: %s (%lu/%u cycles)\n",
           observer_complete ? "COMPLETE" : "TIMEOUT",
           (unsigned long)captured_cycles, ISSUE112_CYCLE_LIMIT);
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n",
           first_reset_vector_ok ? "PASS" : "FAIL");
    printf("ENTRY 1000:0000 FETCH: %s\n", entry_seen ? "PASS" : "FAIL");
    printf("CONTINUOUS READS: %lu cycles observed\n",
           (unsigned long)read_cycles);
    printf("WORD WRITE 10800: %s\n", write_seen ? "PASS" : "FAIL");
    printf("WRITE DATA 1234 @ R2: %s\n", write_data_ok ? "PASS" : "FAIL");
    printf("PIPELINE IDLE: lowctl=%s low=%s highctl=%s high=%s ptr=%s sram=%s\n",
           low_control_done ? "DONE" : "TIMEOUT",
           low_lookup_done ? "DONE" : "TIMEOUT",
           high_control_done ? "DONE" : "TIMEOUT",
           high_lookup_done ? "DONE" : "TIMEOUT",
           pointer_done ? "DONE" : "TIMEOUT",
           raw_done ? "DONE" : "TIMEOUT");
    printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
    printf("NOTE: write is observed only; C6 does not commit it to SRAM.\n");

    while (true) tight_loop_contents();
}
