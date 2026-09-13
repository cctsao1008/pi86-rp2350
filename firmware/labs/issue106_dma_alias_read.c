/*
 * Issue #106 Phase B2: repeated dynamic SRAM reads through a DMA trigger alias.
 *
 * Engineering laboratory only. The active transaction path is entirely
 * hardware-paced:
 *
 *   pointer feeder DMA
 *       -> source PIO
 *       -> control DMA
 *       -> data-channel AL3_READ_ADDR_TRIG
 *       -> internal SRAM read
 *       -> sink PIO
 *       -> capture DMA
 *       -> result buffer
 *
 * The source and sink PIO state machines use a PIO IRQ flag as a hardware
 * acknowledgement. The next pointer is not published until the previous SRAM
 * word has reached the sink. M33 configures the finite experiment and verifies
 * the result after launch; it does not rearm or service individual transfers.
 *
 * RP2350 DMA reloads TRANS_COUNT from its programmed reload value whenever a
 * channel starts a new transfer sequence. Therefore the data channel can keep
 * TRANS_COUNT=1 programmed once and be started repeatedly by dynamic writes to
 * AL3_READ_ADDR_TRIG.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"
#include "issue106_dma_alias_read.pio.h"
#include "pico/stdlib.h"

#define ISSUE106_VECTOR_COUNT 16u
#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_HANDSHAKE_IRQ 0u

static uint32_t backing[ISSUE106_VECTOR_COUNT];
static uint32_t pointers[ISSUE106_VECTOR_COUNT];
static uint32_t observed[ISSUE106_VECTOR_COUNT];

static bool wait_dma_idle(uint channel, uint64_t deadline) {
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

static void init_source_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config = issue106_pointer_source_program_get_default_config(offset);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_sink_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config = issue106_word_sink_program_get_default_config(offset);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static bool run_repeated_reads(PIO pio, uint source_sm, uint sink_sm,
                               uint pointer_dma, uint control_dma,
                               uint data_dma, uint capture_dma) {
    pio_interrupt_clear(pio, ISSUE106_HANDSHAKE_IRQ);
    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) observed[i] = 0u;

    dma_channel_config data_cfg = dma_channel_get_default_config(data_dma);
    channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&data_cfg, false);
    channel_config_set_write_increment(&data_cfg, false);
    channel_config_set_dreq(&data_cfg, pio_get_dreq(pio, sink_sm, true));
    channel_config_set_high_priority(&data_cfg, true);
    dma_channel_configure(data_dma, &data_cfg,
                          &pio->txf[sink_sm], backing,
                          1u, false);

    dma_channel_config control_cfg = dma_channel_get_default_config(control_dma);
    channel_config_set_transfer_data_size(&control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&control_cfg, false);
    channel_config_set_write_increment(&control_cfg, false);
    channel_config_set_dreq(&control_cfg,
                            pio_get_dreq(pio, source_sm, false));
    channel_config_set_high_priority(&control_cfg, true);
    dma_channel_configure(
        control_dma, &control_cfg,
        &dma_channel_hw_addr(data_dma)->al3_read_addr_trig,
        &pio->rxf[source_sm], ISSUE106_VECTOR_COUNT, false);

    dma_channel_config pointer_cfg = dma_channel_get_default_config(pointer_dma);
    channel_config_set_transfer_data_size(&pointer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&pointer_cfg, true);
    channel_config_set_write_increment(&pointer_cfg, false);
    channel_config_set_dreq(&pointer_cfg,
                            pio_get_dreq(pio, source_sm, true));
    dma_channel_configure(pointer_dma, &pointer_cfg,
                          &pio->txf[source_sm], pointers,
                          ISSUE106_VECTOR_COUNT, false);

    dma_channel_config capture_cfg = dma_channel_get_default_config(capture_dma);
    channel_config_set_transfer_data_size(&capture_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&capture_cfg, false);
    channel_config_set_write_increment(&capture_cfg, true);
    channel_config_set_dreq(&capture_cfg,
                            pio_get_dreq(pio, sink_sm, false));
    dma_channel_configure(capture_dma, &capture_cfg,
                          observed, &pio->rxf[sink_sm],
                          ISSUE106_VECTOR_COUNT, false);

    dma_start_channel_mask((1u << control_dma) | (1u << capture_dma));
    dma_channel_start(pointer_dma);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    if (!wait_dma_idle(pointer_dma, deadline)) return false;
    if (!wait_dma_idle(control_dma, deadline)) return false;
    if (!wait_dma_idle(capture_dma, deadline)) return false;
    if (!wait_dma_idle(data_dma, deadline)) return false;

    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        if (observed[i] != backing[i]) return false;
    }
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);

    printf("issue106 dma-alias repeated-read lab\n");
    printf("scope: autonomous pointer stream -> DMA trigger -> SRAM -> PIO\n");

    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        backing[i] = 0x10600000u ^ (0x01010101u * i) ^ (i << 17);
        pointers[i] = (uint32_t)(uintptr_t)&backing[i];
    }

    PIO pio = pio0;
    const uint source_sm = pio_claim_unused_sm(pio, true);
    const uint sink_sm = pio_claim_unused_sm(pio, true);
    const uint source_offset = pio_add_program(pio, &issue106_pointer_source_program);
    const uint sink_offset = pio_add_program(pio, &issue106_word_sink_program);
    init_source_sm(pio, source_sm, source_offset);
    init_sink_sm(pio, sink_sm, sink_offset);

    const uint pointer_dma = dma_claim_unused_channel(true);
    const uint control_dma = dma_claim_unused_channel(true);
    const uint data_dma = dma_claim_unused_channel(true);
    const uint capture_dma = dma_claim_unused_channel(true);

    const bool pass = run_repeated_reads(
        pio, source_sm, sink_sm,
        pointer_dma, control_dma, data_dma, capture_dma);

    uint failures = 0u;
    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        const bool item_pass = observed[i] == backing[i];
        printf("[%02u] ptr=%08" PRIx32 " expected=%08" PRIx32
               " observed=%08" PRIx32 " %s\n",
               i, pointers[i], backing[i], observed[i],
               item_pass ? "PASS" : "FAIL");
        if (!item_pass) ++failures;
    }

    printf("RESULT: %s (%u/%u data mismatches)\n",
           pass ? "PASS" : "FAIL", failures, ISSUE106_VECTOR_COUNT);
    printf("NOTE: hardware-autonomous repeated read path only; ");
    printf("this is not Intel timing acceptance.\n");

    while (true) tight_loop_contents();
}
