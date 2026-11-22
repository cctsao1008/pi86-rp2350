/*
 * Issue #112 C9: first physical PI-STRESS proof at 4 MHz.
 *
 * This is intentionally still an engineering lab around the physically proven
 * C8C dataplane. It does NOT remove the canonical workload-executor
 * FREE_RUNNING guard and it does NOT claim canonical 20-bit memory.
 *
 * Memory representation:
 *   - 64 KiB logical window mirrored by physical A15..A0
 *   - each logical 16-bit word is stored as one pre-encoded 32-bit scattered
 *     GPIO word
 *
 * That mirror is sufficient for PI-STRESS:
 *   image      physical 10000h -> mirrored 0000h
 *   telemetry physical 10800h -> mirrored 0800h
 *   stack      physical 1FFFEh -> mirrored FFFEh
 *
 * The reset bootstrap initializes SS:SP=1000:FFFE and far-jumps to 1000:0000.
 * The physical Intel 8086 then runs PI-STRESS FREE_RUNNING at 4 MHz for a
 * bounded wall-clock epoch with no M33 per-cycle servicing. After RESET is
 * reasserted, the RP2350 decodes telemetry from mirrored offset 0800h.
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
#include "issue112_c8c_read_latch.pio.h"
#include "intel_8086_clock.pio.h"
#ifdef ISSUE112_STACKLESS_PI
#include "pi_continuous_stackless.h"
#define ISSUE112_PI_IMAGE_DATA pi_continuous_stackless_data
#define ISSUE112_PI_IMAGE_SIZE pi_continuous_stackless_size
#define ISSUE112_PI_IMAGE_SHA256 pi_continuous_stackless_sha256
#else
#include "pi_stress.h"
#define ISSUE112_PI_IMAGE_DATA pi_stress_workload_data
#define ISSUE112_PI_IMAGE_SIZE pi_stress_workload_size
#define ISSUE112_PI_IMAGE_SHA256 pi_stress_workload_sha256
#endif
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#ifndef ISSUE112_PROCESSOR_HZ
#define ISSUE112_PROCESSOR_HZ 4000000u
#endif
#define ISSUE112_TIMEOUT_US 250000u
#ifndef ISSUE112_ACTIVE_RUN_MS
#define ISSUE112_ACTIVE_RUN_MS 1000u
#endif

#ifndef ISSUE112_CONTINUOUS_PI
/* 28-bit DMA transfer count is ample for the bounded proof target. */
#define ISSUE112_BUS_CYCLE_BUDGET 4000000u
#define ISSUE112_WRITE_BUDGET     1000000u
#endif

#define ISSUE112_ADDR_LUT_ENTRIES 16384u
#define ISSUE112_ENCODED_WORDS 32768u
#define ISSUE112_DMA_SNIFF_SUM_MODE 0x0fu

#define ISSUE112_T_MAGIC0       0x0800u
#define ISSUE112_T_MAGIC1       0x0802u
#define ISSUE112_T_VERSION      0x0804u
#define ISSUE112_T_SEQ          0x0806u
#define ISSUE112_T_EPOCH_LO     0x0808u
#define ISSUE112_T_EPOCH_HI     0x080au
#define ISSUE112_T_PI_LO        0x080cu
#define ISSUE112_T_PI_HI        0x080eu
#define ISSUE112_T_EXPECTED_LO  0x0810u
#define ISSUE112_T_EXPECTED_HI  0x0812u
#define ISSUE112_T_DELTA_LO     0x0814u
#define ISSUE112_T_DELTA_HI     0x0816u
#define ISSUE112_T_STATE        0x0818u

#define ISSUE112_MAGIC0 0x4950u
#define ISSUE112_MAGIC1 0x3638u
#define ISSUE112_STATE_PUBLISHED 2u

static uint32_t addr_lut[2][ISSUE112_ADDR_LUT_ENTRIES]
    __attribute__((aligned(65536)));
static uint32_t encoded_backing[ISSUE112_ENCODED_WORDS];
static uint32_t address_high_part;

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

static uint16_t backing_word(uint16_t byte_address) {
    hard_assert((byte_address & 1u) == 0u);
    return decode_ad(encoded_backing[byte_address >> 1u]);
}

#ifdef ISSUE112_CONTINUOUS_PI
typedef struct {
    uint16_t seq;
    uint32_t epoch;
    uint32_t pi_value;
    uint32_t expected;
    int32_t delta;
    uint16_t state;
} issue112_pi_snapshot_t;

static bool read_pi_snapshot(issue112_pi_snapshot_t *snapshot) {
    for (uint attempt = 0u; attempt < 64u; ++attempt) {
        const uint16_t seq0 = backing_word(ISSUE112_T_SEQ);
        if (seq0 == 0u || (seq0 & 1u) != 0u)
            continue;

        const uint16_t magic0 = backing_word(ISSUE112_T_MAGIC0);
        const uint16_t magic1 = backing_word(ISSUE112_T_MAGIC1);
        const uint16_t version = backing_word(ISSUE112_T_VERSION);
        const uint32_t epoch =
            (uint32_t)backing_word(ISSUE112_T_EPOCH_LO) |
            ((uint32_t)backing_word(ISSUE112_T_EPOCH_HI) << 16u);
        const uint32_t pi_value =
            (uint32_t)backing_word(ISSUE112_T_PI_LO) |
            ((uint32_t)backing_word(ISSUE112_T_PI_HI) << 16u);
        const uint32_t expected =
            (uint32_t)backing_word(ISSUE112_T_EXPECTED_LO) |
            ((uint32_t)backing_word(ISSUE112_T_EXPECTED_HI) << 16u);
        const int32_t delta = (int32_t)(
            (uint32_t)backing_word(ISSUE112_T_DELTA_LO) |
            ((uint32_t)backing_word(ISSUE112_T_DELTA_HI) << 16u));
        const uint16_t state = backing_word(ISSUE112_T_STATE);
        const uint16_t seq1 = backing_word(ISSUE112_T_SEQ);

        if (seq0 == seq1 && (seq1 & 1u) == 0u &&
            magic0 == ISSUE112_MAGIC0 && magic1 == ISSUE112_MAGIC1 &&
            version == 1u) {
            snapshot->seq = seq1;
            snapshot->epoch = epoch;
            snapshot->pi_value = pi_value;
            snapshot->expected = expected;
            snapshot->delta = delta;
            snapshot->state = state;
            return true;
        }
    }
    return false;
}

static void print_q8_24(uint32_t value) {
    const uint32_t whole = value >> 24u;
    const uint32_t fraction = (uint32_t)(
        ((uint64_t)(value & 0x00ffffffu) * 100000000ull) >> 24u);
    printf("%" PRIu32 ".%08" PRIu32, whole, fraction);
}
#endif

static void prepare_memory(void) {
    const uintptr_t backing_base = (uintptr_t)encoded_backing;

    for (uint i = 0u; i < ISSUE112_ENCODED_WORDS; ++i)
        encoded_backing[i] = encode_word(0x9090u);

    /*
     * RESET bootstrap, 16 bytes total:
     *
     *   cli
     *   mov ax,1000h
     *   mov ss,ax
     *   mov sp,0fffeh
     *   jmp far 1000:0000
     *   nop
     *   nop
     *
     * This gives PI-STRESS its manifest stack without requiring an external
     * register-loading mechanism.
     */
    static const uint8_t reset_stub[16] = {
        0xfau,
        0xb8u, 0x00u, 0x10u,
        0x8eu, 0xd0u,
        0xbcu, 0xfeu, 0xffu,
        0xeau, 0x00u, 0x00u, 0x00u, 0x10u,
        0x90u, 0x90u
    };
    load_bytes(0xfff0u, reset_stub, sizeof reset_stub);

    hard_assert((ISSUE112_PI_IMAGE_SIZE & 1u) == 0u);
    hard_assert(ISSUE112_PI_IMAGE_SIZE <= 0xfff0u);
    load_bytes(0x0000u, ISSUE112_PI_IMAGE_DATA, ISSUE112_PI_IMAGE_SIZE);

    /*
     * C5/C8 speed-first translation. Each table contributes disjoint pieces
     * of the byte offset of encoded_backing[(A15..A0 & ~1) >> 1].
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
        issue112_c8c_addr_frontend_program_get_default_config(offset);
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
        issue112_c8c_responder_program_get_default_config(offset);
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

static void init_pointer_router(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        issue112_c8c_pointer_router_program_get_default_config(offset);
    sm_config_set_jmp_pin(&c, RP86_PROCESSOR_PIN_BUFRW);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

#ifdef ISSUE112_STACKLESS_PI
static void init_pi_out_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config cfg =
        issue112_pi_out_observer_program_get_default_config(offset);
    sm_config_set_in_pins(&cfg, 0u);
    sm_config_set_jmp_pin(&cfg, RP86_PROCESSOR_PIN_BUFRW);
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(pio, sm, offset, &cfg) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void print_q3_13(uint16_t value) {
    const uint32_t whole = value >> 13u;
    const uint32_t fraction = (uint32_t)(
        ((uint64_t)(value & 0x1fffu) * 100000000ull) >> 13u);
    printf("%" PRIu32 ".%08" PRIu32, whole, fraction);
}
#endif

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

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

#ifdef ISSUE112_CONTINUOUS_PI
    printf("RP86 continuous PI workload @ 2MHz\n");
#ifdef ISSUE112_STACKLESS_PI
    printf("scope: physical 8086/V30 runs stackless PI continuously; no auto-reset\n");
#else
    printf("scope: physical 8086/V30 runs PI-STRESS continuously; no auto-reset\n");
#endif
#else
    printf("issue112 C9 physical PI-STRESS %uMHz lab\n",
           ISSUE112_PROCESSOR_HZ / 1000000u);
    printf("scope: sustained WORD R/W dataplane -> native PI-STRESS\n");
#endif
    printf("processor clock: %u Hz (engineering baseline; Intel compliance not claimed)\n",
           ISSUE112_PROCESSOR_HZ);
    printf("PI image: %u bytes sha256=%s\n",
           (unsigned)ISSUE112_PI_IMAGE_SIZE, ISSUE112_PI_IMAGE_SHA256);

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
        pio_add_program(frontend_pio, &issue112_c8c_addr_frontend_program);
    init_frontend(frontend_pio, frontend0_sm, frontend_offset, 0u, lut0_base);
    init_frontend(frontend_pio, frontend1_sm, frontend_offset, 14u, lut1_base);

    PIO io_pio = pio2;
    const uint responder_sm = pio_claim_unused_sm(io_pio, true);
    const uint pointer_router_sm = pio_claim_unused_sm(io_pio, true);
    const uint responder_offset =
        pio_add_program(io_pio, &issue112_c8c_responder_program);
    const uint pointer_router_offset =
        pio_add_program(io_pio, &issue112_c8c_pointer_router_program);
    init_responder(io_pio, responder_sm, responder_offset);
    init_pointer_router(io_pio, pointer_router_sm, pointer_router_offset);

    PIO clock_pio = pio1;
#ifdef ISSUE112_STACKLESS_PI
    const uint pi_out_observer_sm = pio_claim_unused_sm(clock_pio, true);
    const uint pi_out_observer_offset =
        pio_add_program(clock_pio, &issue112_pi_out_observer_program);
    init_pi_out_observer(
        clock_pio, pi_out_observer_sm, pi_out_observer_offset);
#endif
    const uint clock_sm = pio_claim_unused_sm(clock_pio, true);
    const uint clock_offset =
        pio_add_program(clock_pio, &rp86_intel_8086_clock_program);

    const uint low_control_dma = dma_claim_unused_channel(true);
    const uint low_lookup_dma = dma_claim_unused_channel(true);
    const uint high_control_dma = dma_claim_unused_channel(true);
    const uint high_lookup_dma = dma_claim_unused_channel(true);
    const uint pointer_dma = dma_claim_unused_channel(true);
    const uint raw_data_dma = dma_claim_unused_channel(true);
    const uint pointer_echo_dma = dma_claim_unused_channel(true);
    const uint write_control_dma = dma_claim_unused_channel(true);
    const uint write_data_dma = dma_claim_unused_channel(true);

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
        &frontend_pio->rxf[frontend0_sm],
#ifdef ISSUE112_CONTINUOUS_PI
        dma_encode_endless_transfer_count(), false);
#else
        ISSUE112_BUS_CYCLE_BUDGET, false);
#endif

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
    channel_config_set_chain_to(&raw_cfg, pointer_echo_dma);
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

    dma_channel_config pointer_echo_cfg =
        dma_channel_get_default_config(pointer_echo_dma);
    channel_config_set_transfer_data_size(&pointer_echo_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&pointer_echo_cfg, false);
    channel_config_set_write_increment(&pointer_echo_cfg, false);
    channel_config_set_dreq(
        &pointer_echo_cfg,
        pio_get_dreq(io_pio, pointer_router_sm, true));
    channel_config_set_high_priority(&pointer_echo_cfg, false);
    dma_channel_configure(
        pointer_echo_dma, &pointer_echo_cfg,
        &io_pio->txf[pointer_router_sm],
        &dma_channel_hw_addr(raw_data_dma)->read_addr,
        1u, false);

    dma_channel_config write_data_cfg =
        dma_channel_get_default_config(write_data_dma);
    channel_config_set_transfer_data_size(&write_data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&write_data_cfg, false);
    channel_config_set_write_increment(&write_data_cfg, false);
    channel_config_set_dreq(
        &write_data_cfg, pio_get_dreq(io_pio, responder_sm, false));
    channel_config_set_high_priority(&write_data_cfg, true);
    dma_channel_configure(
        write_data_dma, &write_data_cfg,
        &encoded_backing[0], &io_pio->rxf[responder_sm],
        1u, false);

    dma_channel_config write_control_cfg =
        dma_channel_get_default_config(write_control_dma);
    channel_config_set_transfer_data_size(&write_control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&write_control_cfg, false);
    channel_config_set_write_increment(&write_control_cfg, false);
    channel_config_set_dreq(
        &write_control_cfg,
        pio_get_dreq(io_pio, pointer_router_sm, false));
    channel_config_set_high_priority(&write_control_cfg, true);
    dma_channel_configure(
        write_control_dma, &write_control_cfg,
        &dma_channel_hw_addr(write_data_dma)->al2_write_addr_trig,
        &io_pio->rxf[pointer_router_sm],
#ifdef ISSUE112_CONTINUOUS_PI
        dma_encode_endless_transfer_count(), false);
#else
        ISSUE112_WRITE_BUDGET, false);
#endif

    dma_sniffer_set_data_accumulator(0u);
    dma_sniffer_enable(high_lookup_dma, ISSUE112_DMA_SNIFF_SUM_MODE, false);

    dma_channel_start(low_control_dma);
    dma_channel_start(write_control_dma);

    pio_sm_restart(frontend_pio, frontend0_sm);
    pio_sm_exec(frontend_pio, frontend0_sm, pio_encode_jmp(frontend_offset));
    pio_sm_restart(frontend_pio, frontend1_sm);
    pio_sm_exec(frontend_pio, frontend1_sm, pio_encode_jmp(frontend_offset));
    pio_sm_restart(io_pio, responder_sm);
    pio_sm_exec(io_pio, responder_sm, pio_encode_jmp(responder_offset));
    pio_sm_restart(io_pio, pointer_router_sm);
    pio_sm_exec(
        io_pio, pointer_router_sm, pio_encode_jmp(pointer_router_offset));

    pio_sm_set_enabled(frontend_pio, frontend0_sm, true);
    pio_sm_set_enabled(frontend_pio, frontend1_sm, true);
    pio_sm_set_enabled(io_pio, responder_sm, true);
    pio_sm_set_enabled(io_pio, pointer_router_sm, true);

#ifdef ISSUE112_STACKLESS_PI
    pio_sm_restart(clock_pio, pi_out_observer_sm);
    pio_sm_exec(clock_pio, pi_out_observer_sm,
                pio_encode_jmp(pi_out_observer_offset));
    pio_sm_set_enabled(clock_pio, pi_out_observer_sm, true);
#endif

    init_clock(clock_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

#ifdef ISSUE112_CONTINUOUS_PI
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("RUN MODE: CONTINUOUS\n");
    printf("DMA MODE: ENDLESS\n");
    printf("CPU remains FREE_RUNNING until power/reset.\n");

#ifdef ISSUE112_STACKLESS_PI
    printf("WITNESS: OUT DX,AL; port address carries Q3.13 PI result\n");
    printf("EXPECTED PORT: 6488 (3.1416015625)\n");

    uint64_t total_out = 0u;
    uint64_t total_good = 0u;
    uint64_t total_bad = 0u;
    uint32_t interval_out = 0u;
    uint32_t interval_good = 0u;
    uint16_t last_port = 0u;
    uint64_t next_report = time_us_64() + 1000000u;

    while (true) {
        while (!pio_sm_is_rx_fifo_empty(clock_pio, pi_out_observer_sm)) {
            const uint32_t raw =
                pio_sm_get(clock_pio, pi_out_observer_sm);
            const uint16_t port = decode_ad(raw);
            last_port = port;
            ++total_out;
            ++interval_out;
            if (port == 0x6488u) {
                ++total_good;
                ++interval_good;
            } else {
                ++total_bad;
            }
        }

        const uint64_t now = time_us_64();
        if (now >= next_report) {
            printf("PI OUT/s=%" PRIu32 " good=%" PRIu32
                   " last=%04x value=",
                   interval_out, interval_good, last_port);
            print_q3_13(last_port);
            printf(" total=%" PRIu64 " good_total=%" PRIu64
                   " bad_total=%" PRIu64 "\n",
                   total_out, total_good, total_bad);
            interval_out = 0u;
            interval_good = 0u;
            next_report += 1000000u;
        }
        tight_loop_contents();
    }
#else
    uint32_t last_epoch = 0xffffffffu;
    while (true) {
        sleep_ms(1000u);

        issue112_pi_snapshot_t snapshot;
        if (!read_pi_snapshot(&snapshot)) {
            printf("PI: waiting for stable telemetry... seq=%u state=%u\n",
                   backing_word(ISSUE112_T_SEQ),
                   backing_word(ISSUE112_T_STATE));
            continue;
        }

        if (snapshot.epoch != last_epoch) {
            printf("PI epoch=%" PRIu32 " seq=%u value=",
                   snapshot.epoch, snapshot.seq);
            print_q8_24(snapshot.pi_value);
            printf(" Q8.24=%08" PRIx32 " delta=%" PRId32
                   " state=%u%s\n",
                   snapshot.pi_value, snapshot.delta, snapshot.state,
                   snapshot.state == ISSUE112_STATE_PUBLISHED
                       ? " PUBLISHED" : "");
            last_epoch = snapshot.epoch;
        } else {
            printf("PI epoch=%" PRIu32 " (still computing)\n",
                   snapshot.epoch);
        }
    }
#endif
#else
    /*
     * No M33 cycle servicing and no active telemetry polling. Let the physical
     * processor and PIO/DMA dataplane run autonomously for the proof epoch.
     */
    sleep_ms(ISSUE112_ACTIVE_RUN_MS);

    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    sleep_us(100u);
    pio_sm_set_enabled(clock_pio, clock_sm, false);

    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_gpio[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = RP86_PROCESSOR_AD_BUS_MASK;

    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint16_t magic0 = backing_word(ISSUE112_T_MAGIC0);
    const uint16_t magic1 = backing_word(ISSUE112_T_MAGIC1);
    const uint16_t version = backing_word(ISSUE112_T_VERSION);
    const uint16_t seq = backing_word(ISSUE112_T_SEQ);
    const uint32_t epoch =
        (uint32_t)backing_word(ISSUE112_T_EPOCH_LO) |
        ((uint32_t)backing_word(ISSUE112_T_EPOCH_HI) << 16u);
    const uint32_t pi_value =
        (uint32_t)backing_word(ISSUE112_T_PI_LO) |
        ((uint32_t)backing_word(ISSUE112_T_PI_HI) << 16u);
    const uint32_t expected =
        (uint32_t)backing_word(ISSUE112_T_EXPECTED_LO) |
        ((uint32_t)backing_word(ISSUE112_T_EXPECTED_HI) << 16u);
    const int32_t delta = (int32_t)(
        (uint32_t)backing_word(ISSUE112_T_DELTA_LO) |
        ((uint32_t)backing_word(ISSUE112_T_DELTA_HI) << 16u));
    const uint16_t state = backing_word(ISSUE112_T_STATE);

    const uint32_t cycles_remaining =
        dma_channel_hw_addr(low_control_dma)->transfer_count & 0x0fffffffu;
    const uint32_t cycles_serviced =
        ISSUE112_BUS_CYCLE_BUDGET - cycles_remaining;
    const uint32_t writes_remaining =
        dma_channel_hw_addr(write_control_dma)->transfer_count & 0x0fffffffu;
    const uint32_t writes_serviced =
        ISSUE112_WRITE_BUDGET - writes_remaining;

    const bool telemetry_magic_ok =
        magic0 == ISSUE112_MAGIC0 && magic1 == ISSUE112_MAGIC1;
    const bool seq_ok = seq != 0u && (seq & 1u) == 0u;
    const bool epoch_ok = epoch != 0u;
    const bool state_ok = state == ISSUE112_STATE_PUBLISHED;
    const bool pass =
        reset_clock_ok && telemetry_magic_ok && version == 1u &&
        seq_ok && epoch_ok && state_ok;

    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("BUS CYCLES SERVICED: %" PRIu32 "\n", cycles_serviced);
    printf("WORD WRITES SERVICED: %" PRIu32 "\n", writes_serviced);
    printf("TELEMETRY MAGIC: %04x %04x %s\n",
           magic0, magic1, telemetry_magic_ok ? "PASS" : "FAIL");
    printf("TELEMETRY VERSION: %u %s\n",
           version, version == 1u ? "PASS" : "FAIL");
    printf("TELEMETRY SEQ: %u %s\n",
           seq, seq_ok ? "PASS" : "FAIL");
    printf("TELEMETRY EPOCH: %" PRIu32 " %s\n",
           epoch, epoch_ok ? "PASS" : "FAIL");
    printf("TELEMETRY STATE: %u %s\n",
           state, state_ok ? "PASS" : "FAIL");
    printf("PI Q8.24: actual=%08" PRIx32 " expected=%08" PRIx32
           " delta=%" PRId32 "\n",
           pi_value, expected, delta);
    printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
    printf("NOTE: physical PI-STRESS proof on 64KiB mirrored speed-first memory; "
           "canonical runtime integration remains pending.\n");

    while (true) tight_loop_contents();
#endif
}
