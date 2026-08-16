/*
 * PC1-B hardware-only V30 read-data timing sweep at 0.300 MHz.
 *
 * This diagnostic follows the ordered PIO->DMA->SIO reset-vector experiment.
 * It keeps the response architecture fixed and sweeps only the AD ownership
 * window. The purpose is to distinguish "RP2350 can drive the bus" from
 * "V30 actually samples the intended reset-vector word in this window".
 *
 * Critical path for every case:
 *   selected PIO1 start edge   -> RX DREQ -> DMA -> GPIO_OE_SET
 *   selected PIO1 release edge -> RX DREQ -> DMA -> GPIO_OE_CLR
 *   next CLK rise after release-> RX DREQ -> DMA -> GPIO_OUT(next word)
 *
 * M33 performs no per-cycle polling and receives no timing IRQs. PIO0 runs the
 * continuous clock plus an independent passive ALE observer.
 *
 * Cases:
 *   D1-H2, D1-H3, D2-H2, D2-H3, D2-H4, D3-H2
 *
 * Definitions are documented in pc1b_dma_timing_sweep.pio. PASS for a case is
 * actual control-flow evidence:
 *   FFFF0 -> FFFF2 -> FFFF4 -> F0000
 *
 * Canonical Gate 0-12 results are unchanged.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "hardware/dma.h"
#include "pc1b_dma_timing_sweep.pio.h"
#include "perf_ale_observer.pio.h"

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} pc1b_sweep_sm_t;

typedef pio_sm_config (*pc1b_cfg_fn_t)(uint offset);

typedef struct {
    const char *name;
    const struct pio_program *start_program;
    pc1b_cfg_fn_t start_config;
    const struct pio_program *release_program;
    pc1b_cfg_fn_t release_config;
} pc1b_sweep_case_t;

typedef struct {
    bool reset_ok;
    bool dma_ok;
    bool flow_ok;
    uint32_t start_remaining;
    uint32_t release_remaining;
    uint32_t stage_remaining;
    uint observed_count;
    uint32_t observed[4];
} pc1b_sweep_result_t;

static const uint16_t g_sweep_words[3] = {
    0x00EAu,
    0x0000u,
    0x90F0u,
};

static uint32_t g_sweep_encoded[3];

static const pc1b_sweep_case_t g_cases[] = {
    {
        "D1-H2",
        &pc1b_sweep_d1_start_program,
        pc1b_sweep_d1_start_program_get_default_config,
        &pc1b_sweep_d1_h2_release_program,
        pc1b_sweep_d1_h2_release_program_get_default_config,
    },
    {
        "D1-H3",
        &pc1b_sweep_d1_start_program,
        pc1b_sweep_d1_start_program_get_default_config,
        &pc1b_sweep_d1_h3_release_program,
        pc1b_sweep_d1_h3_release_program_get_default_config,
    },
    {
        "D2-H2",
        &pc1b_sweep_d2_start_program,
        pc1b_sweep_d2_start_program_get_default_config,
        &pc1b_sweep_d2_h2_release_program,
        pc1b_sweep_d2_h2_release_program_get_default_config,
    },
    {
        "D2-H3",
        &pc1b_sweep_d2_start_program,
        pc1b_sweep_d2_start_program_get_default_config,
        &pc1b_sweep_d2_h3_release_program,
        pc1b_sweep_d2_h3_release_program_get_default_config,
    },
    {
        "D2-H4",
        &pc1b_sweep_d2_start_program,
        pc1b_sweep_d2_start_program_get_default_config,
        &pc1b_sweep_d2_h4_release_program,
        pc1b_sweep_d2_h4_release_program_get_default_config,
    },
    {
        "D3-H2",
        &pc1b_sweep_d3_start_program,
        pc1b_sweep_d3_start_program_get_default_config,
        &pc1b_sweep_d3_h2_release_program,
        pc1b_sweep_d3_h2_release_program_get_default_config,
    },
};

static uint32_t pc1b_sweep_encode_word(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static void pc1b_sweep_observer_init(pc1b_sweep_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_sweep_observer_start(pc1b_sweep_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void pc1b_sweep_observer_stop(pc1b_sweep_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void pc1b_sweep_init_program(pc1b_sweep_sm_t *s,
                                    const struct pio_program *program,
                                    pc1b_cfg_fn_t config_fn) {
    s->offset = pio_add_program(s->pio, program);
    pio_sm_config c = config_fn(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_sweep_sm_start(pc1b_sweep_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void pc1b_sweep_sm_stop(pc1b_sweep_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static int pc1b_sweep_configure_rx_to_reg(pc1b_sweep_sm_t *s,
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

static uint32_t pc1b_sweep_dma_remaining(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static bool pc1b_sweep_flow_ok(const pc1b_sweep_result_t *r) {
    return r->observed_count >= 4u &&
           decode_address(r->observed[0]) == 0xFFFF0u &&
           decode_address(r->observed[1]) == 0xFFFF2u &&
           decode_address(r->observed[2]) == 0xFFFF4u &&
           decode_address(r->observed[3]) == 0xF0000u;
}

static pc1b_sweep_result_t pc1b_run_case(perf_clock_t *clock,
                                         pc1b_sweep_sm_t *observer,
                                         pc1b_sweep_sm_t *start_sm,
                                         pc1b_sweep_sm_t *release_sm,
                                         pc1b_sweep_sm_t *stage_sm,
                                         const pc1b_sweep_case_t *tc) {
    pc1b_sweep_result_t r = {0};

    /* No other state machine/program is used on PIO1 in this target. */
    pio_sm_set_enabled(pio1, start_sm->sm, false);
    pio_sm_set_enabled(pio1, release_sm->sm, false);
    pio_sm_set_enabled(pio1, stage_sm->sm, false);
    pio_clear_instruction_memory(pio1);
    pio_interrupt_clear(pio1, 4u);

    pc1b_sweep_init_program(start_sm, tc->start_program, tc->start_config);
    pc1b_sweep_init_program(release_sm, tc->release_program, tc->release_config);
    pc1b_sweep_init_program(stage_sm,
                            &pc1b_sweep_stage_program,
                            pc1b_sweep_stage_program_get_default_config);

    /* Start/release OSR keeps one constant AD OE mask across all three cycles. */
    pio_sm_put_blocking(pio1, start_sm->sm, V30_AD_BUS_MASK);
    pio_sm_put_blocking(pio1, release_sm->sm, V30_AD_BUS_MASK);

    /* Ordered staging after each selected release edge. */
    pio_sm_put_blocking(pio1, stage_sm->sm, g_sweep_encoded[1]);
    pio_sm_put_blocking(pio1, stage_sm->sm, g_sweep_encoded[2]);
    pio_sm_put_blocking(pio1, stage_sm->sm, g_sweep_encoded[2]);

    /* FFFF0 response is preloaded while AD remains high-Z. */
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = g_sweep_encoded[0];
    release_ad();

    const int dma_start = pc1b_sweep_configure_rx_to_reg(
        start_sm, &sio_hw->gpio_oe_set, 3u);
    const int dma_release = pc1b_sweep_configure_rx_to_reg(
        release_sm, &sio_hw->gpio_oe_clr, 3u);
    const int dma_stage = pc1b_sweep_configure_rx_to_reg(
        stage_sm, &sio_hw->gpio_out, 3u);

    set_intr(false);
    hold_reset(true);
    release_ad();

    pc1b_sweep_observer_start(observer);
    pc1b_sweep_sm_start(start_sm);
    pc1b_sweep_sm_start(release_sm);
    pc1b_sweep_sm_start(stage_sm);
    perf_clock_start(clock);

    r.reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (r.reset_ok) {
        /* Restore first response after reset clock counting, before RESET falls. */
        sio_hw->gpio_clr = V30_AD_BUS_MASK;
        sio_hw->gpio_set = g_sweep_encoded[0];
        hold_reset(false);
    }

    const uint64_t deadline = time_us_64() + timeout_us_from_clocks(128u);
    while (time_us_64() <= deadline &&
           pio_sm_get_rx_fifo_level(observer->pio, observer->sm) < 4u) {
        tight_loop_contents();
    }

    /* Immediate safe termination between cases. */
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);
    pc1b_sweep_sm_stop(start_sm);
    pc1b_sweep_sm_stop(release_sm);
    pc1b_sweep_sm_stop(stage_sm);
    pc1b_sweep_observer_stop(observer);

    dma_channel_abort((uint)dma_start);
    dma_channel_abort((uint)dma_release);
    dma_channel_abort((uint)dma_stage);

    r.start_remaining = pc1b_sweep_dma_remaining(dma_start);
    r.release_remaining = pc1b_sweep_dma_remaining(dma_release);
    r.stage_remaining = pc1b_sweep_dma_remaining(dma_stage);
    r.dma_ok = r.start_remaining == 0u &&
               r.release_remaining == 0u &&
               r.stage_remaining == 0u;

    while (!pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
           r.observed_count < 4u) {
        r.observed[r.observed_count++] =
            pio_sm_get(observer->pio, observer->sm);
    }
    r.flow_ok = pc1b_sweep_flow_ok(&r);

    dma_channel_unclaim((uint)dma_start);
    dma_channel_unclaim((uint)dma_release);
    dma_channel_unclaim((uint)dma_stage);

    /* Leave every externally visible driven signal in the safe idle state. */
    hold_reset(true);
    release_ad();
    set_intr(false);
    pio_interrupt_clear(pio1, 4u);

    return r;
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    for (uint i = 0u; i < 3u; ++i)
        g_sweep_encoded[i] = pc1b_sweep_encode_word(g_sweep_words[i]);

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B PIO->DMA->SIO V30 read-data timing sweep - 0.300 MHz\n");
    printf("Clock             : continuous PIO0 free-run\n");
    printf("Observer          : independent passive ALE capture on PIO0\n");
    printf("Critical response : PIO1 RX DREQ -> DMA -> SIO\n");
    printf("M33 per-cycle work: none (no polling, no IRQ)\n");
    printf("Stage policy      : next CLK rise after selected release edge\n");
    printf("Reset-vector code : EA 00 00 00 F0 90\n");
    printf("PASS discriminator: FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("Cases             : D1-H2 D1-H3 D2-H2 D2-H3 D2-H4 D3-H2\n");
    printf("Canonical gate    : unchanged\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    pc1b_sweep_sm_t observer;
    pc1b_sweep_observer_init(&observer);

    pc1b_sweep_sm_t start_sm = {.pio = pio1};
    pc1b_sweep_sm_t release_sm = {.pio = pio1};
    pc1b_sweep_sm_t stage_sm = {.pio = pio1};
    start_sm.sm = pio_claim_unused_sm(pio1, true);
    release_sm.sm = pio_claim_unused_sm(pio1, true);
    stage_sm.sm = pio_claim_unused_sm(pio1, true);

    uint pass_count = 0u;

    for (uint i = 0u; i < (uint)(sizeof(g_cases) / sizeof(g_cases[0])); ++i) {
        const pc1b_sweep_case_t *tc = &g_cases[i];
        const pc1b_sweep_result_t r = pc1b_run_case(
            &clock, &observer, &start_sm, &release_sm, &stage_sm, tc);
        const bool pass = r.reset_ok && r.dma_ok && r.flow_ok;
        if (pass) ++pass_count;

        printf("case %-5s reset=%s dma=%lu/%lu/%lu ALE=",
               tc->name,
               r.reset_ok ? "PASS" : "FAIL",
               (unsigned long)r.start_remaining,
               (unsigned long)r.release_remaining,
               (unsigned long)r.stage_remaining);

        if (r.observed_count == 0u) {
            printf("<none>");
        } else {
            for (uint j = 0u; j < r.observed_count; ++j) {
                if (j != 0u) printf("->");
                printf("%05lX",
                       (unsigned long)decode_address(r.observed[j]));
            }
        }
        printf("  %s\n", pass ? "PASS" : "FAIL");
        fflush(stdout);

        /* Short quiet interval while RESET is asserted before the next case. */
        sleep_ms(10);
    }

    printf("\nTiming sweep PASS cases = %u / %u\n",
           pass_count,
           (unsigned)(sizeof(g_cases) / sizeof(g_cases[0])));
    if (pass_count == 0u) {
        printf("Interpretation           = no tested hardware-only window produced the\n");
        printf("                           reset-vector far jump; next step is direct\n");
        printf("                           physical timing measurement / finer sweep.\n");
    } else {
        printf("Interpretation           = at least one V30-consumed read window exists;\n");
        printf("                           use passing cases to narrow the production\n");
        printf("                           bus-service timing architecture.\n");
    }
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
