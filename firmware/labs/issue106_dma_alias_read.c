/*
 * Issue #106 Phase B1: one-shot dynamic SRAM read through a DMA trigger alias.
 *
 * This is an engineering laboratory, not production firmware.  It proves the
 * narrow primitive needed by the continuous-clock Intel 8086 datapath:
 *
 *   PIO-produced full SRAM pointer
 *       -> control DMA
 *       -> data-channel AL3_READ_ADDR_TRIG
 *       -> internal SRAM read
 *       -> sink PIO TX FIFO
 *
 * M33 prepares and verifies a finite trial, but does not participate in the
 * active pointer -> trigger -> SRAM -> PIO transfer path.
 *
 * This experiment deliberately does NOT yet prove:
 *   - scattered Pi86 GPIO address repacking;
 *   - autonomous multi-transaction DMA rearming;
 *   - Intel 2 MHz bus timing acceptance;
 *   - byte-lane handling;
 *   - physical 8086 execution.
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

static uint32_t backing[ISSUE106_VECTOR_COUNT];

static bool wait_dma_idle(uint channel, uint64_t deadline) {
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

static bool wait_rx_word(PIO pio, uint sm, uint32_t *value,
                         uint64_t deadline) {
    while (pio_sm_is_rx_fifo_empty(pio, sm)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    *value = pio_sm_get(pio, sm);
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

static bool run_one_shot(PIO pio, uint source_sm, uint sink_sm,
                         uint control_dma, uint data_dma,
                         const uint32_t *source_ptr, uint32_t expected,
                         uint32_t *observed) {
    /*
     * Data channel is fully configured but dormant.  TRANS_COUNT=1 is armed
     * before the trial.  A hardware write to AL3_READ_ADDR_TRIG installs the
     * dynamic source pointer and starts the transfer.
     */
    dma_channel_config data_cfg = dma_channel_get_default_config(data_dma);
    channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&data_cfg, false);
    channel_config_set_write_increment(&data_cfg, false);
    channel_config_set_dreq(&data_cfg, DREQ_FORCE);
    channel_config_set_high_priority(&data_cfg, true);
    dma_channel_configure(data_dma, &data_cfg,
                          &pio->txf[sink_sm], backing,
                          1u, false);

    /*
     * Control channel consumes exactly one pointer from the source PIO RX FIFO
     * and writes it to the data channel trigger alias.  The control channel is
     * paced by the source PIO RX DREQ; it cannot fire before PIO publishes the
     * pointer.
     */
    dma_channel_config control_cfg = dma_channel_get_default_config(control_dma);
    channel_config_set_transfer_data_size(&control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&control_cfg, false);
    channel_config_set_write_increment(&control_cfg, false);
    channel_config_set_dreq(
        &control_cfg, pio_get_dreq(pio, source_sm, false));
    channel_config_set_high_priority(&control_cfg, true);
    dma_channel_configure(
        control_dma, &control_cfg,
        &dma_channel_hw_addr(data_dma)->al3_read_addr_trig,
        &pio->rxf[source_sm], 1u, true);

    /*
     * This M33 write happens before the hardware-paced path begins.  PIO then
     * publishes the full SRAM pointer; from that point through the SRAM read
     * and sink-PIO delivery no M33 callback is involved.
     */
    pio_sm_put_blocking(pio, source_sm, (uint32_t)(uintptr_t)source_ptr);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    if (!wait_dma_idle(control_dma, deadline)) return false;
    if (!wait_dma_idle(data_dma, deadline)) return false;
    if (!wait_rx_word(pio, sink_sm, observed, deadline)) return false;

    return *observed == expected;
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);

    printf("issue106 dma-alias read lab\n");
    printf("scope: one-shot PIO pointer -> DMA trigger -> SRAM -> PIO\n");

    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        backing[i] = 0x10600000u ^ (0x01010101u * i) ^ (i << 17);
    }

    PIO pio = pio0;
    const uint source_sm = pio_claim_unused_sm(pio, true);
    const uint sink_sm = pio_claim_unused_sm(pio, true);
    const uint source_offset = pio_add_program(pio, &issue106_pointer_source_program);
    const uint sink_offset = pio_add_program(pio, &issue106_word_sink_program);
    init_source_sm(pio, source_sm, source_offset);
    init_sink_sm(pio, sink_sm, sink_offset);

    const uint control_dma = dma_claim_unused_channel(true);
    const uint data_dma = dma_claim_unused_channel(true);

    uint failures = 0u;
    for (uint i = 0; i < ISSUE106_VECTOR_COUNT; ++i) {
        uint32_t observed = 0u;
        const bool pass = run_one_shot(
            pio, source_sm, sink_sm, control_dma, data_dma,
            &backing[i], backing[i], &observed);

        printf("[%02u] ptr=%08" PRIx32 " expected=%08" PRIx32
               " observed=%08" PRIx32 " %s\n",
               i, (uint32_t)(uintptr_t)&backing[i], backing[i], observed,
               pass ? "PASS" : "FAIL");
        if (!pass) ++failures;

        dma_channel_abort(control_dma);
        dma_channel_abort(data_dma);
        pio_sm_clear_fifos(pio, source_sm);
        pio_sm_clear_fifos(pio, sink_sm);
    }

    printf("RESULT: %s (%u/%u failed)\n",
           failures == 0u ? "PASS" : "FAIL",
           failures, ISSUE106_VECTOR_COUNT);
    printf("NOTE: this is not Intel timing acceptance.\n");

    while (true) tight_loop_contents();
}
