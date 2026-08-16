/*
 * PC1-B extended control-flow discriminators at 0.300 MHz.
 *
 * Motivation:
 * The earlier PC1-B tests stopped after four ALE observations and treated
 * FFFF0 -> FFFF2 -> FFFF4 -> FFFF6 as a failure.  That observation window is
 * too short to prove that the V30 did not consume a branch instruction because
 * instruction prefetch may continue before execution redirects the bus.
 *
 * This diagnostic keeps the ordered PIO->DMA->SIO response path, services a
 * bounded stream of reset-area reads, and observes up to 16 ALE cycles.
 * It runs two independent cases:
 *
 *   1. FAR-JUMP
 *      Reset bytes: EA 00 00 00 F0 90 ...
 *      PASS: after the initial FFFF0/FFFF2/FFFF4 fetches, F0000 appears
 *      anywhere in the bounded ALE trace.
 *
 *   2. SELF-LOOP
 *      Reset bytes: EB FE 90 90 ...  (JMP $)
 *      PASS: FFFF0 appears again after the initial reset-vector fetch.
 *
 * The M33 only drains the passive observer while the experiment runs.  It does
 * not perform per-cycle bus timing, data lookup, GPIO ownership changes, or IRQ
 * service.  Six DMA channels keep the three PIO1 timing SMs supplied and move
 * their RX events directly to SIO registers.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "../performance_characterization_1/pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "hardware/dma.h"
#include "pc1b_dma_ordered_events.pio.h"
#include "perf_ale_observer.pio.h"

#define EXT_TRACE_DEPTH       16u
#define EXT_SERVICE_CYCLES    16u
#define EXT_TIMEOUT_CLOCKS    320u

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} ext_sm_t;

typedef struct {
    const char *name;
    uint16_t first_word;
    uint16_t staged_words[EXT_SERVICE_CYCLES];
    bool far_jump_case;
} ext_case_t;

typedef struct {
    bool reset_ok;
    bool dma_ok;
    bool pass;
    uint32_t observed[EXT_TRACE_DEPTH];
    uint observed_count;
    uint32_t tx_start_remaining;
    uint32_t tx_release_remaining;
    uint32_t tx_stage_remaining;
    uint32_t rx_start_remaining;
    uint32_t rx_release_remaining;
    uint32_t rx_stage_remaining;
} ext_result_t;

static uint32_t g_encoded_nop;
static uint32_t g_encoded_mask_words[EXT_SERVICE_CYCLES];

static uint32_t encode_word(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static void init_start_sm(ext_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_dma_ordered_start_program);
    pio_sm_config c = pc1b_dma_ordered_start_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_release_sm(ext_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_dma_ordered_release_program);
    pio_sm_config c = pc1b_dma_ordered_release_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_stage_sm(ext_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_dma_ordered_stage_program);
    pio_sm_config c = pc1b_dma_ordered_stage_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_observer_sm(ext_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void restart_sm(ext_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void stop_sm(ext_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static int config_mem_to_tx(ext_sm_t *s, const uint32_t *src, uint count) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, true));
    dma_channel_configure((uint)ch,
                          &cfg,
                          &s->pio->txf[s->sm],
                          src,
                          count,
                          true);
    return ch;
}

static int config_rx_to_reg(ext_sm_t *s, volatile void *write_addr, uint count) {
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
                          count,
                          true);
    return ch;
}

static uint32_t remaining(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static void stop_dma6(const int ch[6]) {
    for (uint i = 0u; i < 6u; ++i) dma_channel_abort((uint)ch[i]);
}

static void unclaim_dma6(const int ch[6]) {
    for (uint i = 0u; i < 6u; ++i) dma_channel_unclaim((uint)ch[i]);
}

static bool initial_reset_fetch_ok(const ext_result_t *r) {
    return r->observed_count >= 3u &&
           decode_address(r->observed[0]) == 0xFFFF0u &&
           decode_address(r->observed[1]) == 0xFFFF2u &&
           decode_address(r->observed[2]) == 0xFFFF4u;
}

static bool detect_far_jump(const ext_result_t *r) {
    if (!initial_reset_fetch_ok(r)) return false;
    for (uint i = 3u; i < r->observed_count; ++i) {
        if (decode_address(r->observed[i]) == 0xF0000u) return true;
    }
    return false;
}

static bool detect_self_loop(const ext_result_t *r) {
    if (r->observed_count < 2u || decode_address(r->observed[0]) != 0xFFFF0u)
        return false;
    for (uint i = 1u; i < r->observed_count; ++i) {
        if (decode_address(r->observed[i]) == 0xFFFF0u) return true;
    }
    return false;
}

static ext_result_t run_case(const ext_case_t *tc,
                             perf_clock_t *clock,
                             ext_sm_t *start_sm,
                             ext_sm_t *release_sm,
                             ext_sm_t *stage_sm,
                             ext_sm_t *observer_sm) {
    ext_result_t r = {0};
    uint32_t stage_encoded[EXT_SERVICE_CYCLES];
    for (uint i = 0u; i < EXT_SERVICE_CYCLES; ++i)
        stage_encoded[i] = encode_word(tc->staged_words[i]);

    /*
     * Existing ordered PIO programs pull one TX word per serviced bus cycle.
     * Feed those TX FIFOs from RAM using DMA so the timing path stays fully
     * hardware paced for the entire extended observation interval.
     */
    int ch[6];
    ch[0] = config_mem_to_tx(start_sm, g_encoded_mask_words, EXT_SERVICE_CYCLES);
    ch[1] = config_mem_to_tx(release_sm, g_encoded_mask_words, EXT_SERVICE_CYCLES);
    ch[2] = config_mem_to_tx(stage_sm, stage_encoded, EXT_SERVICE_CYCLES);
    ch[3] = config_rx_to_reg(start_sm, &sio_hw->gpio_oe_set, EXT_SERVICE_CYCLES);
    ch[4] = config_rx_to_reg(release_sm, &sio_hw->gpio_oe_clr, EXT_SERVICE_CYCLES);
    ch[5] = config_rx_to_reg(stage_sm, &sio_hw->gpio_out, EXT_SERVICE_CYCLES);

    set_intr(false);
    hold_reset(true);
    release_ad();

    restart_sm(observer_sm);
    restart_sm(start_sm);
    restart_sm(release_sm);
    restart_sm(stage_sm);
    perf_clock_start(clock);

    r.reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (r.reset_ok) {
        const uint32_t first_encoded = encode_word(tc->first_word);
        sio_hw->gpio_clr = V30_AD_BUS_MASK;
        sio_hw->gpio_set = first_encoded;
        hold_reset(false);
    }

    const uint64_t deadline = time_us_64() + timeout_us_from_clocks(EXT_TIMEOUT_CLOCKS);
    bool control_flow_seen = false;

    while (time_us_64() <= deadline && r.observed_count < EXT_TRACE_DEPTH) {
        while (!pio_sm_is_rx_fifo_empty(observer_sm->pio, observer_sm->sm) &&
               r.observed_count < EXT_TRACE_DEPTH) {
            const uint32_t raw = pio_sm_get(observer_sm->pio, observer_sm->sm);
            r.observed[r.observed_count++] = raw;

            const uint32_t addr = decode_address(raw);
            if (tc->far_jump_case) {
                if (r.observed_count > 3u && addr == 0xF0000u)
                    control_flow_seen = true;
            } else {
                if (r.observed_count > 1u && addr == 0xFFFF0u)
                    control_flow_seen = true;
            }
        }

        /* Capture a short tail after the redirect instead of stopping on it. */
        if (control_flow_seen && r.observed_count >= 8u) break;
        tight_loop_contents();
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);
    stop_sm(start_sm);
    stop_sm(release_sm);
    stop_sm(stage_sm);
    stop_sm(observer_sm);

    stop_dma6(ch);

    while (!pio_sm_is_rx_fifo_empty(observer_sm->pio, observer_sm->sm) &&
           r.observed_count < EXT_TRACE_DEPTH) {
        r.observed[r.observed_count++] = pio_sm_get(observer_sm->pio, observer_sm->sm);
    }

    r.tx_start_remaining = remaining(ch[0]);
    r.tx_release_remaining = remaining(ch[1]);
    r.tx_stage_remaining = remaining(ch[2]);
    r.rx_start_remaining = remaining(ch[3]);
    r.rx_release_remaining = remaining(ch[4]);
    r.rx_stage_remaining = remaining(ch[5]);

    /*
     * A redirect may stop the test before all 16 prepared transactions are
     * consumed, so zero remaining count is not required for PASS.  What matters
     * is that paired TX/RX progress remains coherent for each timing SM.
     */
    r.dma_ok = r.tx_start_remaining == r.rx_start_remaining &&
               r.tx_release_remaining == r.rx_release_remaining &&
               r.tx_stage_remaining == r.rx_stage_remaining;

    r.pass = r.reset_ok && r.dma_ok &&
             (tc->far_jump_case ? detect_far_jump(&r) : detect_self_loop(&r));

    unclaim_dma6(ch);
    return r;
}

static void print_result(const ext_case_t *tc, const ext_result_t *r) {
    printf("\n[%s]\n", tc->name);
    printf("RESET clock count = %s\n", r->reset_ok ? "PASS" : "FAIL");
    printf("DMA remain TX/RX  = start %lu/%lu  release %lu/%lu  stage %lu/%lu\n",
           (unsigned long)r->tx_start_remaining,
           (unsigned long)r->rx_start_remaining,
           (unsigned long)r->tx_release_remaining,
           (unsigned long)r->rx_release_remaining,
           (unsigned long)r->tx_stage_remaining,
           (unsigned long)r->rx_stage_remaining);
    printf("DMA progress      = %s\n", r->dma_ok ? "COHERENT" : "MISMATCH");
    printf("ALE trace (%u):\n", r->observed_count);
    for (uint i = 0u; i < r->observed_count; ++i)
        printf("  %02u = %05lX\n", i,
               (unsigned long)decode_address(r->observed[i]));

    if (tc->far_jump_case)
        printf("Discriminator     = F0000 appears after initial FFFF0/FFFF2/FFFF4\n");
    else
        printf("Discriminator     = FFFF0 reappears after initial reset fetch\n");
    printf("RESULT            = %s\n", r->pass ? "PASS" : "FAIL");
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    g_encoded_nop = encode_word(0x9090u);
    for (uint i = 0u; i < EXT_SERVICE_CYCLES; ++i)
        g_encoded_mask_words[i] = V30_AD_BUS_MASK;

    ext_case_t far_jump = {
        .name = "FAR-JUMP extended observation",
        .first_word = 0x00EAu,
        .far_jump_case = true,
    };
    ext_case_t self_loop = {
        .name = "SELF-LOOP extended observation",
        .first_word = 0xFEEBu,
        .far_jump_case = false,
    };

    /*
     * FAR-JUMP bytes from reset vector:
     *   EA 00 00 00 F0 90 90 ...
     * first_word supplies FFFF0; stage[0] supplies FFFF2; stage[1] FFFF4.
     */
    for (uint i = 0u; i < EXT_SERVICE_CYCLES; ++i)
        far_jump.staged_words[i] = 0x9090u;
    far_jump.staged_words[0] = 0x0000u;
    far_jump.staged_words[1] = 0x90F0u;

    /* SELF-LOOP starts with EB FE (JMP $); later speculative reads get NOPs. */
    for (uint i = 0u; i < EXT_SERVICE_CYCLES; ++i)
        self_loop.staged_words[i] = 0x9090u;

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B extended V30 control-flow observation - 0.300 MHz\n");
    printf("Clock             : continuous PIO0 free-run\n");
    printf("Timing/data       : ordered PIO1 -> DMA -> SIO\n");
    printf("Observer          : passive ALE capture on PIO0\n");
    printf("Observation bound : up to %u ALE cycles\n", EXT_TRACE_DEPTH);
    printf("M33 critical work : none; foreground only drains observer FIFO\n");
    printf("Case A            : far jump EA 00 00 00 F0\n");
    printf("Case B            : self-loop EB FE\n");
    printf("Canonical gate    : unchanged\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    ext_sm_t start_sm;
    ext_sm_t release_sm;
    ext_sm_t stage_sm;
    ext_sm_t observer_sm;
    init_start_sm(&start_sm);
    init_release_sm(&release_sm);
    init_stage_sm(&stage_sm);
    init_observer_sm(&observer_sm);

    const ext_result_t far_result = run_case(
        &far_jump, &clock, &start_sm, &release_sm, &stage_sm, &observer_sm);
    print_result(&far_jump, &far_result);

    sleep_ms(20);

    const ext_result_t loop_result = run_case(
        &self_loop, &clock, &start_sm, &release_sm, &stage_sm, &observer_sm);
    print_result(&self_loop, &loop_result);

    printf("\nEXTENDED CONTROL-FLOW SUMMARY = far-jump %s / self-loop %s\n",
           far_result.pass ? "PASS" : "FAIL",
           loop_result.pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
