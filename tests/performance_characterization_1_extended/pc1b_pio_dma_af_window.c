/*
 * PC1-B physical read-window characterization at 0.300 MHz.
 *
 * This experiment moves data-drive start to ALE-fall (AF), ahead of the
 * previously tested D1/D2/D3 starts.  The response path remains hardware-only:
 * PIO event -> RX DREQ -> DMA -> SIO.  Three release cases are tested:
 * AF-H2, AF-H3, AF-H4.  Next-word staging remains ordered on the first CLK
 * rising edge after the selected release edge.
 *
 * The instruction discriminator uses EB FE (JMP $) at FFFF0.  PASS means
 * FFFF0 reappears after the initial reset fetch within a bounded 16-ALE trace.
 *
 * During AF-H2, an independent PIO0 state machine captures six raw GPIO-bank
 * snapshots across the first reset-vector cycle to show when the driven AD
 * pattern becomes physically visible relative to ALE/CLK edges.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "../performance_characterization_1/pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "hardware/dma.h"
#include "pc1b_af_events.pio.h"
#include "pc1b_first_cycle_phase_capture.pio.h"
#include "perf_ale_observer.pio.h"

#define AF_TRACE_DEPTH       16u
#define AF_SERVICE_CYCLES    16u
#define AF_TIMEOUT_CLOCKS    320u
#define AF_PHASE_COUNT        6u

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} af_sm_t;

typedef struct {
    const char *name;
    uint hold_falls;
} af_case_t;

typedef struct {
    bool reset_ok;
    bool dma_ok;
    bool pass;
    uint32_t observed[AF_TRACE_DEPTH];
    uint observed_count;
    uint32_t phase_raw[AF_PHASE_COUNT];
    uint phase_count;
    uint32_t tx_start_remaining;
    uint32_t tx_release_remaining;
    uint32_t tx_stage_remaining;
    uint32_t rx_start_remaining;
    uint32_t rx_release_remaining;
    uint32_t rx_stage_remaining;
} af_result_t;

static uint32_t g_af_masks[AF_SERVICE_CYCLES];
static uint32_t g_af_stage_words[AF_SERVICE_CYCLES];

static uint32_t encode_word(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static uint16_t raw_to_ad(uint32_t raw) {
    static const uint8_t pins[16] = {
        V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
        V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
        V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
        V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
    };
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (raw & (1u << pins[bit])) value |= (uint16_t)(1u << bit);
    }
    return value;
}

static void init_start_sm(af_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_af_start_program);
    pio_sm_config c = pc1b_af_start_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_release_sm(af_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_af_release_program);
    pio_sm_config c = pc1b_af_release_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_stage_sm(af_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_af_stage_program);
    pio_sm_config c = pc1b_af_stage_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_observer_sm(af_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void init_phase_sm(af_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void restart_sm(af_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void restart_counted_sm(af_sm_t *s, uint hold_falls) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_set(pio_y, hold_falls - 1u));
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void stop_sm(af_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static int config_mem_to_tx(af_sm_t *s, const uint32_t *src, uint count) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, true));
    dma_channel_configure((uint)ch, &cfg, &s->pio->txf[s->sm], src, count, true);
    return ch;
}

static int config_rx_to_reg(af_sm_t *s, volatile void *write_addr, uint count) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, false));
    dma_channel_configure((uint)ch, &cfg, write_addr, &s->pio->rxf[s->sm], count, true);
    return ch;
}

static uint32_t remaining(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static bool self_loop_seen(const af_result_t *r) {
    if (r->observed_count < 2u || decode_address(r->observed[0]) != 0xFFFF0u)
        return false;
    for (uint i = 1u; i < r->observed_count; ++i) {
        if (decode_address(r->observed[i]) == 0xFFFF0u) return true;
    }
    return false;
}

static af_result_t run_case(const af_case_t *tc,
                            bool capture_phase,
                            perf_clock_t *clock,
                            af_sm_t *start_sm,
                            af_sm_t *release_sm,
                            af_sm_t *stage_sm,
                            af_sm_t *observer_sm,
                            af_sm_t *phase_sm) {
    af_result_t r = {0};
    int ch[6];
    ch[0] = config_mem_to_tx(start_sm, g_af_masks, AF_SERVICE_CYCLES);
    ch[1] = config_mem_to_tx(release_sm, g_af_masks, AF_SERVICE_CYCLES);
    ch[2] = config_mem_to_tx(stage_sm, g_af_stage_words, AF_SERVICE_CYCLES);
    ch[3] = config_rx_to_reg(start_sm, &sio_hw->gpio_oe_set, AF_SERVICE_CYCLES);
    ch[4] = config_rx_to_reg(release_sm, &sio_hw->gpio_oe_clr, AF_SERVICE_CYCLES);
    ch[5] = config_rx_to_reg(stage_sm, &sio_hw->gpio_out, AF_SERVICE_CYCLES);

    set_intr(false);
    hold_reset(true);
    release_ad();

    restart_sm(observer_sm);
    if (capture_phase) restart_sm(phase_sm);
    restart_sm(start_sm);
    restart_counted_sm(release_sm, tc->hold_falls);
    restart_counted_sm(stage_sm, tc->hold_falls);
    perf_clock_start(clock);

    r.reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (r.reset_ok) {
        const uint32_t first_encoded = encode_word(0xFEEBu);
        sio_hw->gpio_clr = V30_AD_BUS_MASK;
        sio_hw->gpio_set = first_encoded;
        hold_reset(false);
    }

    const uint64_t deadline = time_us_64() + timeout_us_from_clocks(AF_TIMEOUT_CLOCKS);
    while (time_us_64() <= deadline && r.observed_count < AF_TRACE_DEPTH) {
        while (!pio_sm_is_rx_fifo_empty(observer_sm->pio, observer_sm->sm) &&
               r.observed_count < AF_TRACE_DEPTH) {
            r.observed[r.observed_count++] = pio_sm_get(observer_sm->pio, observer_sm->sm);
        }
        if (self_loop_seen(&r) && r.observed_count >= 8u) break;
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
    if (capture_phase) stop_sm(phase_sm);

    for (uint i = 0u; i < 6u; ++i) dma_channel_abort((uint)ch[i]);

    while (!pio_sm_is_rx_fifo_empty(observer_sm->pio, observer_sm->sm) &&
           r.observed_count < AF_TRACE_DEPTH) {
        r.observed[r.observed_count++] = pio_sm_get(observer_sm->pio, observer_sm->sm);
    }

    if (capture_phase) {
        while (!pio_sm_is_rx_fifo_empty(phase_sm->pio, phase_sm->sm) &&
               r.phase_count < AF_PHASE_COUNT) {
            r.phase_raw[r.phase_count++] = pio_sm_get(phase_sm->pio, phase_sm->sm);
        }
    }

    r.tx_start_remaining = remaining(ch[0]);
    r.tx_release_remaining = remaining(ch[1]);
    r.tx_stage_remaining = remaining(ch[2]);
    r.rx_start_remaining = remaining(ch[3]);
    r.rx_release_remaining = remaining(ch[4]);
    r.rx_stage_remaining = remaining(ch[5]);
    r.dma_ok = r.tx_start_remaining == r.rx_start_remaining &&
               r.tx_release_remaining == r.rx_release_remaining &&
               r.tx_stage_remaining == r.rx_stage_remaining;
    r.pass = r.reset_ok && r.dma_ok && self_loop_seen(&r);

    for (uint i = 0u; i < 6u; ++i) dma_channel_unclaim((uint)ch[i]);
    return r;
}

static void print_case(const af_case_t *tc, const af_result_t *r) {
    printf("\n[%s]\n", tc->name);
    printf("RESET clock count = %s\n", r->reset_ok ? "PASS" : "FAIL");
    printf("DMA remain TX/RX  = start %lu/%lu release %lu/%lu stage %lu/%lu\n",
           (unsigned long)r->tx_start_remaining, (unsigned long)r->rx_start_remaining,
           (unsigned long)r->tx_release_remaining, (unsigned long)r->rx_release_remaining,
           (unsigned long)r->tx_stage_remaining, (unsigned long)r->rx_stage_remaining);
    printf("DMA progress      = %s\n", r->dma_ok ? "COHERENT" : "MISMATCH");
    printf("ALE trace (%u):\n", r->observed_count);
    for (uint i = 0u; i < r->observed_count; ++i)
        printf("  %02u = %05lX\n", i, (unsigned long)decode_address(r->observed[i]));
    printf("Discriminator     = FFFF0 reappears after initial reset fetch\n");
    printf("RESULT            = %s\n", r->pass ? "PASS" : "FAIL");
}

static void print_phase_capture(const af_result_t *r) {
    static const char *const names[AF_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    printf("\n[FIRST-CYCLE PHYSICAL GPIO SNAPSHOTS - AF-H2]\n");
    printf("phase raw_gpio  ALE CLK IOM DTR BHE AD16\n");
    for (uint i = 0u; i < r->phase_count; ++i) {
        const uint32_t raw = r->phase_raw[i];
        printf("%-4s  %08lX   %u   %u   %u   %u   %u  %04X\n",
               names[i],
               (unsigned long)raw,
               (unsigned)((raw >> V30_PIN_ALE) & 1u),
               (unsigned)((raw >> V30_PIN_CLK) & 1u),
               (unsigned)((raw >> V30_PIN_IOM) & 1u),
               (unsigned)((raw >> V30_PIN_DTR) & 1u),
               (unsigned)((raw >> V30_PIN_BHE) & 1u),
               (unsigned)raw_to_ad(raw));
    }
    printf("Expected first response word = FEEB (EB FE / JMP $)\n");
    printf("Note: snapshots show pad state at discrete PIO sample points; they do not\n");
    printf("      by themselves prove the V30's internal sampling instant.\n");
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    for (uint i = 0u; i < AF_SERVICE_CYCLES; ++i) {
        g_af_masks[i] = V30_AD_BUS_MASK;
        g_af_stage_words[i] = encode_word(0x9090u);
    }

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B Physical Read Window Characterization - 0.300 MHz\n");
    printf("Start timing      : AF = immediate PIO event after ALE fall\n");
    printf("Release cases     : H2 / H3 / H4 falling edges\n");
    printf("Stage policy      : next CLK rise after release\n");
    printf("Critical response : PIO1 RX DREQ -> DMA -> SIO\n");
    printf("Instruction       : EB FE (JMP $) at FFFF0\n");
    printf("Observer          : passive ALE capture on PIO0\n");
    printf("Phase capture     : AF/R1/F1/R2/F2/R3 raw GPIO snapshots on AF-H2\n");
    printf("Canonical gate    : unchanged\n");

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    af_sm_t start_sm, release_sm, stage_sm, observer_sm, phase_sm;
    init_start_sm(&start_sm);
    init_release_sm(&release_sm);
    init_stage_sm(&stage_sm);
    init_observer_sm(&observer_sm);
    init_phase_sm(&phase_sm);

    static const af_case_t cases[] = {
        {"AF-H2", 2u},
        {"AF-H3", 3u},
        {"AF-H4", 4u},
    };

    uint pass_count = 0u;
    af_result_t first = {0};
    for (uint i = 0u; i < 3u; ++i) {
        const af_result_t r = run_case(&cases[i], i == 0u, &clock,
                                       &start_sm, &release_sm, &stage_sm,
                                       &observer_sm, &phase_sm);
        print_case(&cases[i], &r);
        if (i == 0u) first = r;
        if (r.pass) ++pass_count;
        sleep_ms(20);
    }

    print_phase_capture(&first);
    printf("\nAF timing PASS cases = %u / 3\n", pass_count);
    if (pass_count == 0u) {
        printf("Interpretation       = ALE-fall start still produced no executed self-loop;\n");
        printf("                       use the phase snapshots to compare actual AD validity\n");
        printf("                       against the V30 read timing before further edge guesses.\n");
    } else {
        printf("Interpretation       = at least one ALE-fall hardware window produced\n");
        printf("                       observable branch execution; retain that window for\n");
        printf("                       the next deterministic bus prototype.\n");
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);
    while (true) tight_loop_contents();
}
