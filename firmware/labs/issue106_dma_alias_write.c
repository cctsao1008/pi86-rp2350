/*
 * Issue #106 Phase B3: repeated dynamic SRAM writes through a DMA trigger alias.
 *
 * Engineering laboratory only. The active transaction path is entirely
 * hardware-paced:
 *
 *   pointer feeder DMA
 *       -> source PIO
 *       -> control DMA
 *       -> data-channel AL2_WRITE_ADDR_TRIG
 *       -> internal SRAM write
 *       -> chained acknowledgement DMA
 *       -> acknowledgement PIO
 *       -> next pointer release
 *
 * M33 prepares a finite destination-pointer vector and a finite data vector,
 * launches the experiment, and verifies SRAM after completion. It does not
 * rearm or service individual pointer -> trigger -> SRAM write transactions.
 *
 * The data DMA keeps TRANS_COUNT=1 programmed once. Each dynamic write to
 * AL2_WRITE_ADDR_TRIG starts a new one-word transfer sequence. Its READ_ADDR
 * increments across the prepared data vector, while WRITE_ADDR is replaced by
 * each dynamically supplied destination pointer. Completion chains to a small
 * acknowledgement DMA, which drives a PIO IRQ handshake before the source PIO
 * releases the next destination pointer.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"
#include "issue106_dma_alias_write.pio.h"
#include "pico/stdlib.h"

#define ISSUE106_VECTOR_COUNT 16u
#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_HANDSHAKE_IRQ 0u
#define ISSUE106_SENTINEL 0xa5a5a5a5u

static uint32_t backing[ISSUE106_VECTOR_COUNT];
static uint32_t values[ISSUE106_VECTOR_COUNT];
static uint32_t pointers[ISSUE106_VECTOR_COUNT];
static const uint32_t ack_token = 0x106b3001u;

static bool wait_dma_idle(uint channel, uint64_t deadline) {
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

static void init_pointer_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        issue106_write_pointer_source_program_get_default_config(offset);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_ack_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        issue106_write_ack_sink_program_get_default_config(offset);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static bool run_repeated_writes(PIO pio, uint pointer_sm, uint ack_sm,
                                uint pointer_dma, uint control_dma,
                                uint data_dma, uint ack_dma) {
    pio_interrupt_clear(pio, ISSUE106_HANDSHAKE_IRQ);

    dma_channel_config ack_cfg = dma_channel_get_default_config(ack_dma);
    channel_config_set_transfer_data_size(&ack_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&ack_cfg, false);
    channel_config_set_write_increment(&ack_cfg, false);
    channel_config_set_dreq(&ack_cfg, pio_get_dreq(pio, ack_sm, true));
    channel_config_set_high_priority(&ack_cfg, true);
    dma_channel_configure(ack_dma, &ack_cfg,
                          &pio->txf[ack_sm], &ack_token,
                          1u, false);

    dma_channel_config data_cfg = dma_channel_get_default_config(data_dma);
    channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&data_cfg, true);
    channel_config_set_write_increment(&data_cfg, false);
    channel_config_set_dreq(&data_cfg, DREQ_FORCE);
    channel_config_set_chain_to(&data_cfg, ack_dma);
    channel_config_set_high_priority(&data_cfg, true);
    dma_channel_configure(data_dma, &data_cfg,
                          backing, values,
                          1u, false);

    dma_channel_config control_cfg = dma_channel_get_default_config(control_dma);
    channel_config_set_transfer_data_size(&control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&control_cfg, false);
    channel_config_set_write_increment(&control_cfg, false);
    channel_config_set_dreq(&control_cfg,
                            pio_get_dreq(pio, pointer_sm, false));
    channel_config_set_high_priority(&control_cfg, true);
    dma_channel_configure(
        control_dma, &control_cfg,
        &dma_channel_hw_addr(data_dma)->al2_write_addr_trig,
        &pio->rxf[pointer_sm], ISSUE106_VECTOR_COUNT, false);

    dma_channel_config pointer_cfg = dma_channel_get_default_config(pointer_dma);
    channel_config_set_transfer_data_size(&pointer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&pointer_cfg, true);
    channel_config_set_write_increment(&pointer_cfg, false);
    channel_config_set_dreq(&pointer_cfg,
                            pio_get_dreq(pio, pointer_sm, true));
    dma_channel_configure(pointer_dma, &pointer_cfg,
                          &pio->txf[pointer_sm], pointers,
                          ISSUE106_VECTOR_COUNT, false);

    dma_channel_start(control_dma);
    dma_channel_start(pointer_dma);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    if (!wait_dma_idle(pointer_dma, deadline)) return false;
    if (!wait_dma_idle(control_dma, deadline)) return false;
    if (!wait_dma_idle(data_dma, deadline)) return false;
    if (!wait_dma_idle(ack_dma, deadline)) return false;

    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        if (backing[i] != values[i]) return false;
    }
    return true;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);

    printf("issue106 dma-alias repeated-write lab\n");
    printf("scope: autonomous pointer stream -> DMA trigger -> SRAM write\n");

    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        backing[i] = ISSUE106_SENTINEL;
        values[i] = 0x106b0000u ^ (0x01020304u * i) ^ (i << 19);
        pointers[i] = (uint32_t)(uintptr_t)&backing[i];
    }

    PIO pio = pio0;
    const uint pointer_sm = pio_claim_unused_sm(pio, true);
    const uint ack_sm = pio_claim_unused_sm(pio, true);
    const uint pointer_offset =
        pio_add_program(pio, &issue106_write_pointer_source_program);
    const uint ack_offset =
        pio_add_program(pio, &issue106_write_ack_sink_program);
    init_pointer_sm(pio, pointer_sm, pointer_offset);
    init_ack_sm(pio, ack_sm, ack_offset);

    const uint pointer_dma = dma_claim_unused_channel(true);
    const uint control_dma = dma_claim_unused_channel(true);
    const uint data_dma = dma_claim_unused_channel(true);
    const uint ack_dma = dma_claim_unused_channel(true);

    const bool pass = run_repeated_writes(
        pio, pointer_sm, ack_sm,
        pointer_dma, control_dma, data_dma, ack_dma);

    uint failures = 0u;
    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        const bool item_pass = backing[i] == values[i];
        printf("[%02u] ptr=%08" PRIx32 " expected=%08" PRIx32
               " observed=%08" PRIx32 " %s\n",
               i, pointers[i], values[i], backing[i],
               item_pass ? "PASS" : "FAIL");
        if (!item_pass) ++failures;
    }

    printf("RESULT: %s (%u/%u data mismatches)\n",
           pass ? "PASS" : "FAIL", failures, ISSUE106_VECTOR_COUNT);
    printf("NOTE: hardware-autonomous repeated write path only; ");
    printf("this is not Intel timing acceptance.\n");

    while (true) tight_loop_contents();
}
