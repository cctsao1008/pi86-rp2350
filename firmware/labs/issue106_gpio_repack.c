/*
 * Issue #106 Phase C1a: PIO scattered-GPIO repack laboratory.
 *
 * This is an engineering-only finite experiment.  M33 prepares synthetic raw
 * GPIO0..27 snapshots that use the canonical Pi86 HAT mapping, then a DMA feeds
 * those snapshots plus a static extraction microcode stream into a PIO state
 * machine.  The PIO emits canonical linear 20-bit processor addresses; a
 * capture DMA stores them for post-run verification.
 *
 * Active repack path after launch:
 *
 *   synthetic raw GPIO bitmap stream
 *       -> feeder DMA
 *       -> repack PIO
 *       -> packed A19..A0
 *       -> capture DMA
 *       -> result buffer
 *
 * No M33 callback participates per address.  This phase deliberately does not
 * claim physical pad capture or Intel timing acceptance.  Its purpose is to
 * prove that the scattered mapping can be converted by RP2350 PIO using a
 * bounded, auditable microcoded extraction primitive before the physical T1
 * capture front end is introduced.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/processor_bus_pins.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "issue106_gpio_repack.pio.h"
#include "pico/stdlib.h"

#define ISSUE106_VECTOR_COUNT 16u
#define ISSUE106_ADDRESS_BITS 20u
#define ISSUE106_STREAM_WORDS_PER_VECTOR (1u + ISSUE106_ADDRESS_BITS + 1u)
#define ISSUE106_STREAM_WORD_COUNT \
    (ISSUE106_VECTOR_COUNT * ISSUE106_STREAM_WORDS_PER_VECTOR)
#define ISSUE106_TIMEOUT_US 100000u

/* Relative instruction index of the final PUSH in issue106_gpio_repack.pio. */
#define ISSUE106_REPACK_FINISH_REL 10u

static uint32_t stream_words[ISSUE106_STREAM_WORD_COUNT];
static uint32_t expected[ISSUE106_VECTOR_COUNT];
static uint32_t observed[ISSUE106_VECTOR_COUNT];

static const uint8_t address_gpio[ISSUE106_ADDRESS_BITS] = {
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
    RP86_PROCESSOR_PIN_A16,
    RP86_PROCESSOR_PIN_A17,
    RP86_PROCESSOR_PIN_A18,
    RP86_PROCESSOR_PIN_A19,
};

static uint32_t encode_raw_address(uint32_t address) {
    uint32_t raw = 0u;
    for (uint bit = 0; bit < ISSUE106_ADDRESS_BITS; ++bit) {
        if ((address >> bit) & 1u) raw |= 1u << address_gpio[bit];
    }
    return raw;
}

static bool wait_dma_idle(uint channel, uint64_t deadline) {
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

static void init_repack_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config = issue106_gpio_repack_program_get_default_config(offset);

    /*
     * OUT shifts right so OUT NULL,n discards GPIO bits below the target and
     * OUT X,1 extracts the target bit.  IN shifts left, and bits are processed
     * A19..A0, yielding the canonical 20-bit address in the low 20 ISR bits.
     */
    sm_config_set_out_shift(&config, true, false, 32u);
    sm_config_set_in_shift(&config, false, false, 32u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);

    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static uint16_t extraction_instruction(uint gpio) {
    /* PIO OUT count 0 encodes 32, so GPIO0 needs a true NOP. */
    return gpio == 0u ? pio_encode_nop() : pio_encode_out(pio_null, gpio);
}

static void build_stream(uint program_offset) {
    const uint16_t finish_jump =
        pio_encode_jmp(program_offset + ISSUE106_REPACK_FINISH_REL);

    uint cursor = 0u;
    for (uint vector = 0; vector < ISSUE106_VECTOR_COUNT; ++vector) {
        /* Non-trivial deterministic 20-bit vectors including boundary cases. */
        uint32_t address;
        switch (vector) {
            case 0: address = 0x00000u; break;
            case 1: address = 0x00001u; break;
            case 2: address = 0x00002u; break;
            case 3: address = 0x0ffffu; break;
            case 4: address = 0x10000u; break;
            case 5: address = 0x7ffffu; break;
            case 6: address = 0x80000u; break;
            case 7: address = 0xffffeu; break;
            case 8: address = 0xfffffu; break;
            default:
                address = (0x13579u * vector) ^ (0x2468au + (vector << 9));
                address &= 0xfffffu;
                break;
        }

        expected[vector] = address;
        observed[vector] = 0u;

        stream_words[cursor++] = encode_raw_address(address);

        /* Process A19 down to A0 so left-shift accumulation preserves value. */
        for (int bit = ISSUE106_ADDRESS_BITS - 1; bit >= 0; --bit) {
            stream_words[cursor++] = extraction_instruction(address_gpio[bit]);
        }

        stream_words[cursor++] = finish_jump;
    }

    hard_assert(cursor == ISSUE106_STREAM_WORD_COUNT);
}

static bool run_repack_lab(PIO pio, uint sm, uint feeder_dma,
                           uint capture_dma) {
    dma_channel_config feeder_cfg = dma_channel_get_default_config(feeder_dma);
    channel_config_set_transfer_data_size(&feeder_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&feeder_cfg, true);
    channel_config_set_write_increment(&feeder_cfg, false);
    channel_config_set_dreq(&feeder_cfg, pio_get_dreq(pio, sm, true));
    channel_config_set_high_priority(&feeder_cfg, true);
    dma_channel_configure(feeder_dma, &feeder_cfg,
                          &pio->txf[sm], stream_words,
                          ISSUE106_STREAM_WORD_COUNT, false);

    dma_channel_config capture_cfg = dma_channel_get_default_config(capture_dma);
    channel_config_set_transfer_data_size(&capture_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&capture_cfg, false);
    channel_config_set_write_increment(&capture_cfg, true);
    channel_config_set_dreq(&capture_cfg, pio_get_dreq(pio, sm, false));
    channel_config_set_high_priority(&capture_cfg, true);
    dma_channel_configure(capture_dma, &capture_cfg,
                          observed, &pio->rxf[sm],
                          ISSUE106_VECTOR_COUNT, false);

    dma_start_channel_mask((1u << capture_dma) | (1u << feeder_dma));

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    if (!wait_dma_idle(feeder_dma, deadline)) return false;
    if (!wait_dma_idle(capture_dma, deadline)) return false;

    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        if ((observed[i] & 0xfffffu) != expected[i]) return false;
    }
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);

    printf("issue106 scattered-GPIO PIO repack lab\n");
    printf("scope: synthetic raw GPIO bitmap -> PIO microcoded repack -> A19..A0\n");

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &issue106_gpio_repack_program);
    init_repack_sm(pio, sm, offset);
    build_stream(offset);

    const uint feeder_dma = dma_claim_unused_channel(true);
    const uint capture_dma = dma_claim_unused_channel(true);

    const bool pass = run_repack_lab(pio, sm, feeder_dma, capture_dma);

    uint failures = 0u;
    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        const uint32_t actual = observed[i] & 0xfffffu;
        const bool item_pass = actual == expected[i];
        printf("[%02u] expected=%05" PRIx32 " observed=%05" PRIx32 " %s\n",
               i, expected[i], actual, item_pass ? "PASS" : "FAIL");
        if (!item_pass) ++failures;
    }

    printf("RESULT: %s (%u/%u address mismatches)\n",
           pass ? "PASS" : "FAIL", failures, ISSUE106_VECTOR_COUNT);
    printf("NOTE: PIO permutation primitive only; physical pad capture and ");
    printf("Intel timing acceptance are not proven.\n");

    while (true) tight_loop_contents();
}
