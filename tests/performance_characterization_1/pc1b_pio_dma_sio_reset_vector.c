/*
 * PC1-B PIO->DMA->SIO reset-vector discriminator at 0.300 MHz.
 *
 * Goal:
 *   Remove M33 exception-entry latency from the V30 read-response path.
 *
 * Timing/data path:
 *   PIO1 start SM   -> RX DREQ -> DMA -> SIO GPIO_OE_SET at D2
 *   PIO1 release SM -> RX DREQ -> DMA -> SIO GPIO_OE_CLR at H2
 *   PIO1 stage SM   -> RX DREQ -> DMA -> SIO GPIO_OUT after H2
 *
 * The AD output latch is preloaded with 00EA before RESET release. After each
 * H2 edge, the stage path prepares the next reset-vector word while AD is
 * transitioning back to high-Z. The CPU performs no per-cycle polling and no
 * IRQ service during the experiment.
 *
 * Reset-vector words:
 *   FFFF0 -> 00EA
 *   FFFF2 -> 0000
 *   FFFF4 -> 90F0
 *
 * PASS discriminator:
 *   passive PIO observes FFFF0 -> FFFF2 -> FFFF4 -> F0000
 *
 * This remains a focused diagnostic. It does not modify canonical Gate 0-12
 * or claim that D2/H2 is a specification-defined V30 timing requirement.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "hardware/dma.h"
#include "pc1b_dma_events.pio.h"
#include "perf_ale_observer.pio.h"

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} pc1b_dma_sm_t;

static const uint16_t g_dma_words[3] = {
    0x00EAu,
    0x0000u,
    0x90F0u,
};

static uint32_t g_dma_encoded[3];

static uint32_t pc1b_dma_encode_word(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static void pc1b_dma_sm_init(pc1b_dma_sm_t *s,
                             const struct pio_program *program,
                             pio_sm_config config) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, program);
    pio_sm_init(s->pio, s->sm, s->offset, &config);
}

static void pc1b_dma_start_sm_init(pc1b_dma_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_dma_start_program);
    pio_sm_config c = pc1b_dma_start_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_dma_release_sm_init(pc1b_dma_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_dma_release_program);
    pio_sm_config c = pc1b_dma_release_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_dma_stage_sm_init(pc1b_dma_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_dma_stage_program);
    pio_sm_config c = pc1b_dma_stage_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_dma_observer_sm_init(pc1b_dma_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_dma_sm_start(pc1b_dma_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void pc1b_dma_sm_stop(pc1b_dma_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void pc1b_dma_put3(pc1b_dma_sm_t *s, uint32_t a, uint32_t b, uint32_t c) {
    pio_sm_put_blocking(s->pio, s->sm, a);
    pio_sm_put_blocking(s->pio, s->sm, b);
    pio_sm_put_blocking(s->pio, s->sm, c);
}

static int pc1b_dma_configure_rx_to_reg(pc1b_dma_sm_t *s,
                                        volatile void *write_addr,
                                        uint transfer_count) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, false));
    dma_channel_configure((uint)ch,
                          &cfg,
                          write_addr,
                          &s->pio->rxf[s->sm],
                          transfer_count,
                          true);
    return ch;
}

static uint32_t pc1b_dma_remaining(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    for (uint i = 0u; i < 3u; ++i)
        g_dma_encoded[i] = pc1b_dma_encode_word(g_dma_words[i]);

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B PIO->DMA->SIO reset-vector prototype - 0.300 MHz\n");
    printf("Clock             : continuous PIO free-run\n");
    printf("D2/H2 timing      : PIO1 state machines\n");
    printf("Critical response : PIO RX DREQ -> DMA -> SIO registers\n");
    printf("M33 per-cycle work: none (no polling, no IRQ)\n");
    printf("Start action      : DMA writes GPIO_OE_SET at D2\n");
    printf("Release action    : DMA writes GPIO_OE_CLR at H2\n");
    printf("Next-word stage   : DMA writes GPIO_OUT after H2\n");
    printf("Reset-vector code : EA 00 00 00 F0 90\n");
    printf("PASS discriminator: FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("Canonical gate    : unchanged\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    pc1b_dma_sm_t start_sm;
    pc1b_dma_sm_t release_sm;
    pc1b_dma_sm_t stage_sm;
    pc1b_dma_sm_t observer_sm;
    pc1b_dma_start_sm_init(&start_sm);
    pc1b_dma_release_sm_init(&release_sm);
    pc1b_dma_stage_sm_init(&stage_sm);
    pc1b_dma_observer_sm_init(&observer_sm);

    /*
     * Event payloads. Start/release always use the AD OE mask. Stage prepares
     * word1 after cycle0, word2 after cycle1, and leaves word2 after cycle2.
     */
    pc1b_dma_put3(&start_sm, V30_AD_BUS_MASK, V30_AD_BUS_MASK, V30_AD_BUS_MASK);
    pc1b_dma_put3(&release_sm, V30_AD_BUS_MASK, V30_AD_BUS_MASK, V30_AD_BUS_MASK);
    pc1b_dma_put3(&stage_sm, g_dma_encoded[1], g_dma_encoded[2], g_dma_encoded[2]);

    /* Preload FFFF0 response while AD remains high-Z. */
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = g_dma_encoded[0];
    release_ad();

    const int dma_start = pc1b_dma_configure_rx_to_reg(
        &start_sm, &sio_hw->gpio_oe_set, 3u);
    const int dma_release = pc1b_dma_configure_rx_to_reg(
        &release_sm, &sio_hw->gpio_oe_clr, 3u);
    const int dma_stage = pc1b_dma_configure_rx_to_reg(
        &stage_sm, &sio_hw->gpio_out, 3u);

    set_intr(false);
    hold_reset(true);
    release_ad();

    pc1b_dma_sm_start(&observer_sm);
    pc1b_dma_sm_start(&start_sm);
    pc1b_dma_sm_start(&release_sm);
    pc1b_dma_sm_start(&stage_sm);
    perf_clock_start(&clock);

    const bool reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (reset_ok) {
        /* Restore the preloaded first response after RESET clock counting. */
        sio_hw->gpio_clr = V30_AD_BUS_MASK;
        sio_hw->gpio_set = g_dma_encoded[0];
        hold_reset(false);
    }

    const uint64_t deadline = time_us_64() + timeout_us_from_clocks(96u);
    while (time_us_64() <= deadline &&
           pio_sm_get_rx_fifo_level(observer_sm.pio, observer_sm.sm) < 4u) {
        tight_loop_contents();
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    pc1b_dma_sm_stop(&start_sm);
    pc1b_dma_sm_stop(&release_sm);
    pc1b_dma_sm_stop(&stage_sm);
    pc1b_dma_sm_stop(&observer_sm);

    dma_channel_abort((uint)dma_start);
    dma_channel_abort((uint)dma_release);
    dma_channel_abort((uint)dma_stage);

    uint32_t observed[8] = {0};
    uint observed_count = 0u;
    while (!pio_sm_is_rx_fifo_empty(observer_sm.pio, observer_sm.sm) &&
           observed_count < 8u) {
        observed[observed_count++] = pio_sm_get(observer_sm.pio, observer_sm.sm);
    }

    const uint32_t start_remaining = pc1b_dma_remaining(dma_start);
    const uint32_t release_remaining = pc1b_dma_remaining(dma_release);
    const uint32_t stage_remaining = pc1b_dma_remaining(dma_stage);

    dma_channel_unclaim((uint)dma_start);
    dma_channel_unclaim((uint)dma_release);
    dma_channel_unclaim((uint)dma_stage);

    const bool flow_ok = observed_count >= 4u &&
                         decode_address(observed[0]) == 0xFFFF0u &&
                         decode_address(observed[1]) == 0xFFFF2u &&
                         decode_address(observed[2]) == 0xFFFF4u &&
                         decode_address(observed[3]) == 0xF0000u;

    const bool dma_ok = start_remaining == 0u &&
                        release_remaining == 0u &&
                        stage_remaining == 0u;
    const bool pass = reset_ok && dma_ok && flow_ok;

    printf("RESET clock count = %s\n", reset_ok ? "PASS" : "FAIL");
    printf("DMA start remain  = %lu\n", (unsigned long)start_remaining);
    printf("DMA release remain= %lu\n", (unsigned long)release_remaining);
    printf("DMA stage remain  = %lu\n", (unsigned long)stage_remaining);

    printf("\nPIO ALE sequence (%u captured):\n", observed_count);
    for (uint i = 0u; i < observed_count; ++i) {
        printf("  PIO%u = %05lX\n",
               i,
               (unsigned long)decode_address(observed[i]));
    }

    printf("\nExpected control-flow sequence = FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("PC1-B PIO->DMA->SIO RESULT     = %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        printf("Interpretation                    = CPU exception latency is removed;\n");
        printf("                                    a remaining failure requires checking\n");
        printf("                                    D2/H2 timing, DMA/SIO ordering, or\n");
        printf("                                    physical V30 read sampling directly.\n");
    }
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
