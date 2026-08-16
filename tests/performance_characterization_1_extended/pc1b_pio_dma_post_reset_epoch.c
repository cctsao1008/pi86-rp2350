/*
 * PC1-B post-reset epoch controlled discriminator at 0.300 MHz.
 *
 * Purpose:
 *   Revalidate the hardware-only AF-H2 response path with RESET qualification
 *   fully separated from the measurement epoch. No observer, response PIO SM,
 *   phase-capture SM, or DMA channel is active while RESET qualification clocks
 *   run.
 *
 * Measurement epoch:
 *   1. RESET=HIGH, AD high-Z, all timing/observer SMs disabled.
 *   2. Run RESET qualification clocks using only the clock SM.
 *   3. Stop CLK at LOW.
 *   4. Reset/arm PIO state, preload FEEB, then configure DMA.
 *   5. Verify RX DMA has made zero pre-release progress.
 *   6. Enable timing/observer SMs while CLK remains LOW.
 *   7. Deassert RESET, then restart continuous CLK.
 *
 * Instruction discriminator:
 *   FFFF0 -> EB FE (JMP $)
 *   PASS  -> FFFF0 reappears after the initial reset fetch.
 *
 * AF-H2 timing:
 *   start   = immediate event after ALE fall
 *   release = second falling CLK edge after ALE fall
 *   stage   = next rising CLK edge after release
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

#define EPOCH_TRACE_DEPTH       16u
#define EPOCH_SERVICE_CYCLES    16u
#define EPOCH_TIMEOUT_CLOCKS    320u
#define EPOCH_PHASE_COUNT        6u
#define EPOCH_HOLD_FALLS         2u

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} epoch_sm_t;

typedef struct {
    bool reset_ok;
    bool tx_primed;
    bool pre_release_clean;
    bool first_ale_ok;
    bool dma_coherent;
    bool self_loop;
    uint32_t observed[EPOCH_TRACE_DEPTH];
    uint observed_count;
    uint32_t phase_raw[EPOCH_PHASE_COUNT];
    uint phase_count;
    uint32_t pre_tx[3];
    uint32_t pre_rx[3];
    uint32_t post_tx[3];
    uint32_t post_rx[3];
} epoch_result_t;

static uint32_t g_masks[EPOCH_SERVICE_CYCLES];
static uint32_t g_stage_words[EPOCH_SERVICE_CYCLES];

static uint32_t encode_word_epoch(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static uint16_t raw_to_ad_epoch(uint32_t raw) {
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

static void init_start_sm_epoch(epoch_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_af_start_program);
    pio_sm_config c = pc1b_af_start_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void init_release_sm_epoch(epoch_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_af_release_program);
    pio_sm_config c = pc1b_af_release_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void init_stage_sm_epoch(epoch_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_af_stage_program);
    pio_sm_config c = pc1b_af_stage_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void init_observer_sm_epoch(epoch_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void init_phase_sm_epoch(epoch_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void arm_sm_epoch(epoch_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
}

static void arm_counted_sm_epoch(epoch_sm_t *s, uint hold_falls) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_set(pio_y, hold_falls - 1u));
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
}

static void enable_sm_epoch(epoch_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void stop_sm_epoch(epoch_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static int config_mem_to_tx_epoch(epoch_sm_t *s, const uint32_t *src, uint count) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, true));
    dma_channel_configure((uint)ch, &cfg, &s->pio->txf[s->sm], src, count, true);
    return ch;
}

static int config_rx_to_reg_epoch(epoch_sm_t *s, volatile void *write_addr, uint count) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, false));
    dma_channel_configure((uint)ch, &cfg, write_addr, &s->pio->rxf[s->sm], count, true);
    return ch;
}

static uint32_t remaining_epoch(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static bool wait_tx_primed(epoch_sm_t *a, epoch_sm_t *b, epoch_sm_t *c) {
    const uint64_t deadline = time_us_64() + 200u;
    while (time_us_64() <= deadline) {
        if (pio_sm_get_tx_fifo_level(a->pio, a->sm) > 0u &&
            pio_sm_get_tx_fifo_level(b->pio, b->sm) > 0u &&
            pio_sm_get_tx_fifo_level(c->pio, c->sm) > 0u) {
            return true;
        }
        tight_loop_contents();
    }
    return false;
}

static bool self_loop_seen_epoch(const epoch_result_t *r) {
    if (r->observed_count < 2u || decode_address(r->observed[0]) != 0xFFFF0u)
        return false;
    for (uint i = 1u; i < r->observed_count; ++i) {
        if (decode_address(r->observed[i]) == 0xFFFF0u) return true;
    }
    return false;
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    for (uint i = 0u; i < EPOCH_SERVICE_CYCLES; ++i) {
        g_masks[i] = V30_AD_BUS_MASK;
        g_stage_words[i] = encode_word_epoch(0x9090u);
    }

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B Post-Reset Epoch Controlled Test - 0.300 MHz\n");
    printf("RESET qualification: clock-only; all response/observer SMs disabled\n");
    printf("Measurement epoch  : arm after RESET clocks with CLK stopped LOW\n");
    printf("Timing             : AF-H2, ordered stage on next rise\n");
    printf("Instruction        : EB FE (JMP $) at FFFF0\n");
    printf("Critical response  : PIO1 RX DREQ -> DMA -> SIO\n");
    printf("Observer/phase     : PIO0, armed only after RESET qualification\n");
    printf("Canonical gate     : unchanged\n\n");

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    epoch_sm_t start_sm, release_sm, stage_sm, observer_sm, phase_sm;
    init_start_sm_epoch(&start_sm);
    init_release_sm_epoch(&release_sm);
    init_stage_sm_epoch(&stage_sm);
    init_observer_sm_epoch(&observer_sm);
    init_phase_sm_epoch(&phase_sm);

    epoch_result_t r = {0};

    /* RESET qualification epoch: clock only. */
    set_intr(false);
    hold_reset(true);
    release_ad();
    perf_clock_start(&clock);
    r.reset_ok = wait_reset_clocks(RESET_CLOCKS);
    perf_clock_stop(&clock); /* forces CLK back to GPIO LOW */

    /* Measurement epoch setup while RESET remains asserted and CLK is LOW. */
    arm_sm_epoch(&observer_sm);
    arm_sm_epoch(&phase_sm);
    arm_sm_epoch(&start_sm);
    arm_counted_sm_epoch(&release_sm, EPOCH_HOLD_FALLS);
    arm_counted_sm_epoch(&stage_sm, EPOCH_HOLD_FALLS);

    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encode_word_epoch(0xFEEBu);
    release_ad();

    int ch[6];
    ch[0] = config_mem_to_tx_epoch(&start_sm, g_masks, EPOCH_SERVICE_CYCLES);
    ch[1] = config_mem_to_tx_epoch(&release_sm, g_masks, EPOCH_SERVICE_CYCLES);
    ch[2] = config_mem_to_tx_epoch(&stage_sm, g_stage_words, EPOCH_SERVICE_CYCLES);
    ch[3] = config_rx_to_reg_epoch(&start_sm, &sio_hw->gpio_oe_set, EPOCH_SERVICE_CYCLES);
    ch[4] = config_rx_to_reg_epoch(&release_sm, &sio_hw->gpio_oe_clr, EPOCH_SERVICE_CYCLES);
    ch[5] = config_rx_to_reg_epoch(&stage_sm, &sio_hw->gpio_out, EPOCH_SERVICE_CYCLES);

    r.tx_primed = wait_tx_primed(&start_sm, &release_sm, &stage_sm);
    for (uint i = 0u; i < 3u; ++i) {
        r.pre_tx[i] = remaining_epoch(ch[i]);
        r.pre_rx[i] = remaining_epoch(ch[i + 3u]);
    }

    r.pre_release_clean = r.pre_rx[0] == EPOCH_SERVICE_CYCLES &&
                          r.pre_rx[1] == EPOCH_SERVICE_CYCLES &&
                          r.pre_rx[2] == EPOCH_SERVICE_CYCLES &&
                          pio_sm_is_rx_fifo_empty(start_sm.pio, start_sm.sm) &&
                          pio_sm_is_rx_fifo_empty(release_sm.pio, release_sm.sm) &&
                          pio_sm_is_rx_fifo_empty(stage_sm.pio, stage_sm.sm) &&
                          pio_sm_is_rx_fifo_empty(observer_sm.pio, observer_sm.sm) &&
                          pio_sm_is_rx_fifo_empty(phase_sm.pio, phase_sm.sm);

    /* Arm all measurement SMs while the clock is still stopped LOW. */
    enable_sm_epoch(&observer_sm);
    enable_sm_epoch(&phase_sm);
    enable_sm_epoch(&start_sm);
    enable_sm_epoch(&release_sm);
    enable_sm_epoch(&stage_sm);

    /* Start the post-reset measurement epoch. */
    if (r.reset_ok && r.tx_primed && r.pre_release_clean) {
        hold_reset(false);
        perf_clock_start(&clock);

        const uint64_t deadline = time_us_64() + timeout_us_from_clocks(EPOCH_TIMEOUT_CLOCKS);
        while (time_us_64() <= deadline && r.observed_count < EPOCH_TRACE_DEPTH) {
            while (!pio_sm_is_rx_fifo_empty(observer_sm.pio, observer_sm.sm) &&
                   r.observed_count < EPOCH_TRACE_DEPTH) {
                r.observed[r.observed_count++] = pio_sm_get(observer_sm.pio, observer_sm.sm);
            }
            if (self_loop_seen_epoch(&r) && r.observed_count >= 8u) break;
            tight_loop_contents();
        }
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    stop_sm_epoch(&start_sm);
    stop_sm_epoch(&release_sm);
    stop_sm_epoch(&stage_sm);
    stop_sm_epoch(&observer_sm);
    stop_sm_epoch(&phase_sm);

    for (uint i = 0u; i < 6u; ++i) dma_channel_abort((uint)ch[i]);

    while (!pio_sm_is_rx_fifo_empty(observer_sm.pio, observer_sm.sm) &&
           r.observed_count < EPOCH_TRACE_DEPTH) {
        r.observed[r.observed_count++] = pio_sm_get(observer_sm.pio, observer_sm.sm);
    }
    while (!pio_sm_is_rx_fifo_empty(phase_sm.pio, phase_sm.sm) &&
           r.phase_count < EPOCH_PHASE_COUNT) {
        r.phase_raw[r.phase_count++] = pio_sm_get(phase_sm.pio, phase_sm.sm);
    }

    for (uint i = 0u; i < 3u; ++i) {
        r.post_tx[i] = remaining_epoch(ch[i]);
        r.post_rx[i] = remaining_epoch(ch[i + 3u]);
    }

    r.first_ale_ok = r.observed_count > 0u &&
                     decode_address(r.observed[0]) == 0xFFFF0u;
    r.dma_coherent = r.post_tx[0] == r.post_rx[0] &&
                     r.post_tx[1] == r.post_rx[1] &&
                     r.post_tx[2] == r.post_rx[2];
    r.self_loop = self_loop_seen_epoch(&r);

    printf("RESET clock count      = %s\n", r.reset_ok ? "PASS" : "FAIL");
    printf("TX FIFOs primed        = %s\n", r.tx_primed ? "PASS" : "FAIL");
    printf("PRE-RELEASE DMA remain = TX %lu/%lu/%lu  RX %lu/%lu/%lu\n",
           (unsigned long)r.pre_tx[0], (unsigned long)r.pre_tx[1], (unsigned long)r.pre_tx[2],
           (unsigned long)r.pre_rx[0], (unsigned long)r.pre_rx[1], (unsigned long)r.pre_rx[2]);
    printf("PRE-RESET EVENT LEAK   = %s\n", r.pre_release_clean ? "NO" : "YES / INVALID");
    printf("FIRST post-reset ALE   = %s", r.first_ale_ok ? "FFFF0 PASS" : "FAIL");
    if (r.observed_count > 0u)
        printf(" (observed %05lX)", (unsigned long)decode_address(r.observed[0]));
    printf("\n");

    printf("\nALE trace (%u):\n", r.observed_count);
    for (uint i = 0u; i < r.observed_count; ++i)
        printf("  %02u = %05lX\n", i, (unsigned long)decode_address(r.observed[i]));

    static const char *const phase_names[EPOCH_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    printf("\n[POST-RESET FIRST-CYCLE GPIO SNAPSHOTS]\n");
    printf("phase raw_gpio  ALE CLK IOM DTR BHE AD16\n");
    for (uint i = 0u; i < r.phase_count; ++i) {
        const uint32_t raw = r.phase_raw[i];
        printf("%-4s  %08lX   %u   %u   %u   %u   %u  %04X\n",
               phase_names[i],
               (unsigned long)raw,
               (unsigned)((raw >> V30_PIN_ALE) & 1u),
               (unsigned)((raw >> V30_PIN_CLK) & 1u),
               (unsigned)((raw >> V30_PIN_IOM) & 1u),
               (unsigned)((raw >> V30_PIN_DTR) & 1u),
               (unsigned)((raw >> V30_PIN_BHE) & 1u),
               (unsigned)raw_to_ad_epoch(raw));
    }
    printf("Expected response word = FEEB (EB FE / JMP $)\n");

    printf("\nPOST-RUN DMA remain    = TX %lu/%lu/%lu  RX %lu/%lu/%lu\n",
           (unsigned long)r.post_tx[0], (unsigned long)r.post_tx[1], (unsigned long)r.post_tx[2],
           (unsigned long)r.post_rx[0], (unsigned long)r.post_rx[1], (unsigned long)r.post_rx[2]);
    printf("DMA progress coherent  = %s\n", r.dma_coherent ? "PASS" : "FAIL");
    printf("SELF-LOOP discriminator= %s\n", r.self_loop ? "PASS" : "FAIL");

    const bool valid = r.reset_ok && r.tx_primed && r.pre_release_clean && r.first_ale_ok;
    const bool pass = valid && r.dma_coherent && r.self_loop;
    printf("MEASUREMENT EPOCH      = %s\n", valid ? "VALID" : "INVALID");
    printf("PC1-B POST-RESET RESULT= %s\n", pass ? "PASS" : (valid ? "FAIL" : "INVALID"));
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    for (uint i = 0u; i < 6u; ++i) dma_channel_unclaim((uint)ch[i]);
    while (true) tight_loop_contents();
}
