/*
 * Issue #112 C9D: first physical PI-STRESS proof at 4 MHz.
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
#include "issue112_c9d_write_trace.pio.h"
#include "intel_8086_clock.pio.h"
#ifdef ISSUE112_STACK_PROBE
#include "issue112_c8c_read_latch.pio.h"
#include "issue112_stack_probe.h"
#else
#include "pi_stress.h"
#endif
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#define ISSUE112_PROCESSOR_HZ 4000000u
#define ISSUE112_TIMEOUT_US 250000u
#define ISSUE112_TRACE_WRITE_LIMIT 8192u
#ifdef ISSUE112_STACK_PROBE
#define ISSUE112_TRACE_CYCLE_LIMIT 128u
#define ISSUE112_TRACE_STRIDE 5u
#define ISSUE112_TRACE_WORDS (ISSUE112_TRACE_CYCLE_LIMIT * ISSUE112_TRACE_STRIDE)
#define ISSUE112_OBSERVER_PROGRAM issue112_c8c_observer_program
#define ISSUE112_OBSERVER_CONFIG issue112_c8c_observer_program_get_default_config
#else
#define ISSUE112_TRACE_WORDS (ISSUE112_TRACE_WRITE_LIMIT * 2u)
#define ISSUE112_OBSERVER_PROGRAM issue112_c9d_observer_program
#define ISSUE112_OBSERVER_CONFIG issue112_c9d_observer_program_get_default_config
#endif
#define ISSUE112_TRACE_TIMEOUT_US 1000000u

/* 28-bit DMA transfer count is ample for the bounded one-second proof. */
#define ISSUE112_BUS_CYCLE_BUDGET 4000000u
#define ISSUE112_WRITE_BUDGET     1000000u

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
static uint32_t trace_words[ISSUE112_TRACE_WORDS];

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

static bool is_memory_write(uint32_t raw) {
    return sample_bit(raw, RP86_PROCESSOR_PIN_IOM) != 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) != 0u &&
           sample_bit(raw, RP86_PROCESSOR_PIN_INTAK) != 0u;
}

static const char *lane_name(uint32_t raw) {
    const bool a0 = sample_bit(raw, RP86_PROCESSOR_PIN_AD0) != 0u;
    const bool ube_n = sample_bit(raw, RP86_PROCESSOR_PIN_UBE) != 0u;

    if (!a0 && !ube_n) return "WORD";
    if (!a0 &&  ube_n) return "LOW";
    if ( a0 && !ube_n) return "HIGH";
    return "NONE";
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

static void prepare_memory(void) {
    const uintptr_t backing_base = (uintptr_t)encoded_backing;

    for (uint i = 0u; i < ISSUE112_ENCODED_WORDS; ++i)
        encoded_backing[i] = encode_word(0x9090u);

#ifdef ISSUE112_STACK_INIT_IN_RAM
    /* Keep the stack values and witness identical to C9E. Only move the
     * initialization instructions to 1000:0100, then jump to the witness. */
    static const uint8_t reset_stub[] = {
        0xeau, 0x00u, 0x01u, 0x00u, 0x10u, 0x90u /* jmp far 1000:0100; nop */
    };
    static const uint8_t stack_init[] = {
        0xfau,                       /* cli */
        0xb8u, 0x00u, 0x10u,         /* mov ax,1000h */
        0x8eu, 0xd0u,                /* mov ss,ax */
        0xbcu, 0xfeu, 0xffu,         /* mov sp,0fffeh */
        0xe9u, 0xf4u, 0xfeu          /* jmp near 0000h from next IP 010Ch */
    };
    load_bytes(0xfff0u, reset_stub, sizeof reset_stub);
    load_bytes(0x0100u, stack_init, sizeof stack_init);
#else
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
#endif

#ifdef ISSUE112_STACK_PROBE
    hard_assert((issue112_stack_probe_size & 1u) == 0u);
#ifdef ISSUE112_STACK_INIT_IN_RAM
    hard_assert(issue112_stack_probe_size <= 0x0100u);
#else
    hard_assert(issue112_stack_probe_size <= 0x0800u);
#endif
    load_bytes(0x0000u, issue112_stack_probe_data, issue112_stack_probe_size);
#else
    hard_assert((pi_stress_workload_size & 1u) == 0u);
    hard_assert(pi_stress_workload_size <= 0xfff0u);
    load_bytes(0x0000u, pi_stress_workload_data, pi_stress_workload_size);
#endif

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
    for (uint i = 0u; i < ISSUE112_TRACE_WORDS; ++i)
        trace_words[i] = 0u;
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
        issue112_c9d_addr_frontend_program_get_default_config(offset);
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
        issue112_c9d_responder_program_get_default_config(offset);
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
        issue112_c9d_pointer_router_program_get_default_config(offset);
    sm_config_set_jmp_pin(&c, RP86_PROCESSOR_PIN_BUFRW);
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void init_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config c =
        ISSUE112_OBSERVER_CONFIG(offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, RP86_PROCESSOR_PIN_BUFRW);
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

#ifdef ISSUE112_STACK_PROBE
static bool report_stack_probe(uint32_t cycles) {
    uint32_t captured_writes = 0u;
    for (uint32_t i = 0u; i < cycles; ++i) {
        const uint32_t *sample = &trace_words[i * ISSUE112_TRACE_STRIDE];
        const uint32_t t1 = sample[0];
        const bool write = is_memory_write(t1);
        if (write) ++captured_writes;
        if (i < 72u || write) {
            printf("C[%03" PRIu32 "] %s addr=%05" PRIx32 " lane=%s"
                   " rawT1=%08" PRIx32 " AF=%04x R1=%04x F1=%04x R2=%04x\n",
                   i, write ? "MEMW" : "BUS ", decode_address(t1),
                   lane_name(t1), t1, decode_ad(sample[1]), decode_ad(sample[2]),
                   decode_ad(sample[3]), decode_ad(sample[4]));
        }
    }


    static const struct {
        const char *step;
        uint32_t address;
        uint16_t expected;
    } writes[] = {
        {"SS at entry",      0x10800u, 0x1000u},
        {"SP before PUSH",   0x10802u, 0xfffeu},
        {"PUSH payload",     0x1fffcu, 0xa55au},
        {"SP after PUSH",    0x10804u, 0xfffcu},
        {"POP payload",      0x10806u, 0xa55au},
        {"SP after POP",     0x10808u, 0xfffeu},
        {"CALL return IP",   0x1fffcu, 0x0034u},
        {"SP inside CALL",   0x1080au, 0xfffcu},
        {"SP after RET",     0x1080cu, 0xfffeu},
        {"RET marker",       0x1080eu, 0xc9e1u},
    };
    const uint32_t count = sizeof writes / sizeof writes[0];
    const bool count_ok = captured_writes == count;
    bool pass = count_ok;
    printf("CAPTURED WRITES: %" PRIu32 " expected=%" PRIu32 " %s\n",
           captured_writes, count, count_ok ? "PASS" : "FAIL");

    uint32_t next_cycle = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        uint32_t cycle = next_cycle;
        for (; cycle < cycles; ++cycle) {
            const uint32_t t1 = trace_words[cycle * ISSUE112_TRACE_STRIDE];
            if (is_memory_write(t1) && decode_address(t1) == writes[i].address &&
                sample_bit(t1, RP86_PROCESSOR_PIN_AD0) == 0u &&
                sample_bit(t1, RP86_PROCESSOR_PIN_UBE) == 0u) {
                break;
            }
        }
        bool trace_ok = false;
        if (cycle < cycles) {
            const uint16_t data =
                decode_ad(trace_words[cycle * ISSUE112_TRACE_STRIDE + 4u]);
            trace_ok = data == writes[i].expected;
            printf("%s: addr=%05" PRIx32 " data=%04x expected=%04x %s\n",
                   writes[i].step, writes[i].address, data, writes[i].expected,
                   trace_ok ? "PASS" : "FAIL");
            next_cycle = cycle + 1u;
        } else {
            printf("%s: missing WORD write at %05" PRIx32 " FAIL\n",
                   writes[i].step, writes[i].address);
        }

        /* CALL reuses the PUSH slot, so only validate its final backing value. */
        if (i != 2u) {
            const uint16_t backing = backing_word((uint16_t)writes[i].address);
            const bool backing_ok = backing == writes[i].expected;
            printf("  backing=%04x %s\n", backing, backing_ok ? "PASS" : "FAIL");
            pass = pass && backing_ok;
        }
        pass = pass && trace_ok;
    }
    return pass;
}
#endif

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

#ifdef ISSUE112_STACK_PROBE
#ifdef ISSUE112_STACK_INIT_IN_RAM
    printf("issue112 C9F RAM stack initialization probe @ 4MHz\n");
#else
    printf("issue112 C9E reset fetch and stack probe @ 4MHz\n");
#endif
#else
    printf("issue112 C9D PI-STRESS write-routing discriminator @ 4MHz\n");
    printf("scope: trace physical MEMW address/data vs final backing corruption\n");
#endif
    printf("processor clock: %u Hz (engineering baseline; Intel compliance not claimed)\n",
           ISSUE112_PROCESSOR_HZ);
#ifdef ISSUE112_STACK_PROBE
    printf("Stack probe image: %u bytes sha256=%s\n",
           (unsigned)issue112_stack_probe_size, issue112_stack_probe_sha256);
#else
    printf("PI image: %u bytes sha256=%s\n",
           (unsigned)pi_stress_workload_size, pi_stress_workload_sha256);
#endif

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
        pio_add_program(frontend_pio, &issue112_c9d_addr_frontend_program);
    init_frontend(frontend_pio, frontend0_sm, frontend_offset, 0u, lut0_base);
    init_frontend(frontend_pio, frontend1_sm, frontend_offset, 14u, lut1_base);

    PIO io_pio = pio2;
    const uint responder_sm = pio_claim_unused_sm(io_pio, true);
    const uint pointer_router_sm = pio_claim_unused_sm(io_pio, true);
    const uint responder_offset =
        pio_add_program(io_pio, &issue112_c9d_responder_program);
    const uint pointer_router_offset =
        pio_add_program(io_pio, &issue112_c9d_pointer_router_program);
    init_responder(io_pio, responder_sm, responder_offset);
    init_pointer_router(io_pio, pointer_router_sm, pointer_router_offset);

    PIO clock_pio = pio1;
    const uint observer_sm = pio_claim_unused_sm(clock_pio, true);
    const uint clock_sm = pio_claim_unused_sm(clock_pio, true);
    const uint observer_offset =
        pio_add_program(clock_pio, &ISSUE112_OBSERVER_PROGRAM);
    const uint clock_offset =
        pio_add_program(clock_pio, &rp86_intel_8086_clock_program);
    init_observer(clock_pio, observer_sm, observer_offset);

    const uint low_control_dma = dma_claim_unused_channel(true);
    const uint low_lookup_dma = dma_claim_unused_channel(true);
    const uint high_control_dma = dma_claim_unused_channel(true);
    const uint high_lookup_dma = dma_claim_unused_channel(true);
    const uint pointer_dma = dma_claim_unused_channel(true);
    const uint raw_data_dma = dma_claim_unused_channel(true);
    const uint pointer_echo_dma = dma_claim_unused_channel(true);
    const uint write_control_dma = dma_claim_unused_channel(true);
    const uint write_data_dma = dma_claim_unused_channel(true);
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
        ISSUE112_BUS_CYCLE_BUDGET, false);

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
        ISSUE112_WRITE_BUDGET, false);

    dma_channel_config observer_cfg =
        dma_channel_get_default_config(observer_dma);
    channel_config_set_transfer_data_size(&observer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&observer_cfg, false);
    channel_config_set_write_increment(&observer_cfg, true);
    channel_config_set_dreq(
        &observer_cfg, pio_get_dreq(clock_pio, observer_sm, false));
    channel_config_set_high_priority(&observer_cfg, false);
    dma_channel_configure(
        observer_dma, &observer_cfg,
        trace_words, &clock_pio->rxf[observer_sm],
        ISSUE112_TRACE_WORDS, false);

    dma_sniffer_set_data_accumulator(0u);
    dma_sniffer_enable(high_lookup_dma, ISSUE112_DMA_SNIFF_SUM_MODE, false);

    dma_channel_start(low_control_dma);
    dma_channel_start(write_control_dma);
    dma_channel_start(observer_dma);

    pio_sm_restart(frontend_pio, frontend0_sm);
    pio_sm_exec(frontend_pio, frontend0_sm, pio_encode_jmp(frontend_offset));
    pio_sm_restart(frontend_pio, frontend1_sm);
    pio_sm_exec(frontend_pio, frontend1_sm, pio_encode_jmp(frontend_offset));
    pio_sm_restart(io_pio, responder_sm);
    pio_sm_exec(io_pio, responder_sm, pio_encode_jmp(responder_offset));
    pio_sm_restart(io_pio, pointer_router_sm);
    pio_sm_exec(
        io_pio, pointer_router_sm, pio_encode_jmp(pointer_router_offset));
    pio_sm_restart(clock_pio, observer_sm);
    pio_sm_exec(
        clock_pio, observer_sm, pio_encode_jmp(observer_offset));

    pio_sm_set_enabled(frontend_pio, frontend0_sm, true);
    pio_sm_set_enabled(frontend_pio, frontend1_sm, true);
    pio_sm_set_enabled(io_pio, responder_sm, true);
    pio_sm_set_enabled(io_pio, pointer_router_sm, true);
    pio_sm_set_enabled(clock_pio, observer_sm, true);

    init_clock(clock_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    /*
     * M33 does not service bus cycles. It only waits for the autonomous PIO
     * observer DMA to fill (or time out) before freezing the epoch.
     */
    const uint64_t trace_deadline = time_us_64() + ISSUE112_TRACE_TIMEOUT_US;
    const bool trace_complete = wait_dma_idle(observer_dma, trace_deadline);
    const uint32_t trace_remaining =
        dma_channel_hw_addr(observer_dma)->transfer_count & 0x0fffffffu;

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

#ifdef ISSUE112_STACK_PROBE
    const uint32_t cycles =
        (ISSUE112_TRACE_WORDS - trace_remaining) / ISSUE112_TRACE_STRIDE;
    printf("BUS TRACE: %s (%" PRIu32 "/%u cycles)\n",
           trace_complete ? "COMPLETE" : "TIMEOUT",
           cycles, ISSUE112_TRACE_CYCLE_LIMIT);
    const bool stack_ok = report_stack_probe(cycles);
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("STACK PROBE RESULT: %s\n",
           reset_clock_ok && trace_complete && stack_ok ? "PASS" : "FAIL");
#else
    const uint32_t captured_writes = (ISSUE112_TRACE_WORDS - trace_remaining) / 2u;
    printf("WRITE TRACE: %s (%" PRIu32 "/%u writes)\n",
           trace_complete ? "COMPLETE" : "TIMEOUT",
           captured_writes, ISSUE112_TRACE_WRITE_LIMIT);
    bool saw_10800_1000 = false;
    bool saw_any_10800_after_magic = false;
    uint32_t telemetry_write_count = 0u;
    uint32_t data1000_write_count = 0u;

    for (uint32_t i = 0u; i < captured_writes; ++i) {
        const uint32_t t1 = trace_words[i * 2u];
        const uint32_t r2 = trace_words[i * 2u + 1u];
        if (!is_memory_write(t1)) continue;

        const uint32_t address = decode_address(t1);
        const uint16_t data = decode_ad(r2);
        const uint16_t low = (uint16_t)address;

        const bool telemetry =
            low >= ISSUE112_T_MAGIC0 && low <= ISSUE112_T_STATE;
        const bool data1000 = data == 0x1000u;

        if (telemetry) ++telemetry_write_count;
        if (data1000) ++data1000_write_count;
        if (low == ISSUE112_T_MAGIC0 && data == 0x1000u)
            saw_10800_1000 = true;
        if (low == ISSUE112_T_MAGIC0 && data != ISSUE112_MAGIC0)
            saw_any_10800_after_magic = true;

        if (i < 64u || telemetry || data1000) {
            printf("W[%04" PRIu32 "] addr=%05" PRIx32
                   " low=%04x lane=%s A0=%u UBE#=%u data=%04x%s%s\n",
                   i, address, low, lane_name(t1),
                   (unsigned)sample_bit(t1, RP86_PROCESSOR_PIN_AD0),
                   (unsigned)sample_bit(t1, RP86_PROCESSOR_PIN_UBE),
                   data,
                   telemetry ? " <TELEMETRY>" : "",
                   data1000 ? " <DATA-1000>" : "");
        }
    }

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

    printf("TRACE TELEMETRY WRITES: %" PRIu32 "\n",
           telemetry_write_count);
    printf("TRACE DATA=1000 WRITES: %" PRIu32 "\n",
           data1000_write_count);
    printf("PHYSICAL MEMW low=0800 data=1000: %s\n",
           saw_10800_1000 ? "YES" : "NO");
    printf("PHYSICAL MEMW low=0800 unexpected data: %s\n",
           saw_any_10800_after_magic ? "YES" : "NO");
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
    printf("NOTE: C9D is a routing discriminator; RESULT retains C9 telemetry criteria.\n");

#endif

    while (true) tight_loop_contents();
}
