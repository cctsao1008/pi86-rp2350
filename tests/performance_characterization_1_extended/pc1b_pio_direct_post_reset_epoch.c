/*
 * PC1-B PIO-direct post-reset self-loop discriminator at 0.300 MHz.
 *
 * RESET qualification remains clock-only. In the clean measurement epoch,
 * DMA supplies encoded GPIO0..27 words to one PIO1 TX FIFO; PIO1 writes the
 * pins and PINDIRS directly from ALE-fall through H2. Input synchronizers are
 * deliberately left at their SDK defaults for this baseline.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "../performance_characterization_1/pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "hardware/dma.h"
#include "pc1b_first_cycle_phase_capture.pio.h"
#include "pc1b_pio_direct_ad.pio.h"
#include "perf_ale_observer.pio.h"

#define DIRECT_TRACE_DEPTH       16u
#define DIRECT_SERVICE_CYCLES    16u
#define DIRECT_TIMEOUT_CLOCKS   320u
#define DIRECT_PHASE_COUNT        6u
#define DIRECT_OUT_BASE           0u
#define DIRECT_OUT_COUNT         28u

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} direct_sm_t;

typedef struct {
    bool reset_ok;
    bool tx_primed;
    bool pre_release_clean;
    bool first_ale_ok;
    bool dma_complete;
    bool self_loop;
    uint32_t observed[DIRECT_TRACE_DEPTH];
    uint observed_count;
    uint32_t phase_raw[DIRECT_PHASE_COUNT];
    uint phase_count;
    uint32_t pre_dma;
    uint32_t post_dma;
} direct_result_t;

static uint32_t g_response_words[DIRECT_SERVICE_CYCLES];

static const uint8_t g_ad_pins[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t encode_word_direct(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static uint16_t raw_to_ad_direct(uint32_t raw) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (raw & (1u << g_ad_pins[bit])) value |= (uint16_t)(1u << bit);
    }
    return value;
}

static void init_responder_sm(direct_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_pio_direct_ad_program);
    pio_sm_config c = pc1b_pio_direct_ad_program_get_default_config(s->offset);
    sm_config_set_out_pins(&c, DIRECT_OUT_BASE, DIRECT_OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void route_ad_to_responder(const direct_sm_t *s) {
    for (uint bit = 0u; bit < 16u; ++bit) pio_gpio_init(s->pio, g_ad_pins[bit]);
}

static void route_ad_to_sio(void) {
    for (uint bit = 0u; bit < 16u; ++bit) gpio_set_function(g_ad_pins[bit], GPIO_FUNC_SIO);
}

static void init_observer_sm(direct_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

static void init_phase_sm(direct_sm_t *s) {
    s->pio = pio0;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(s->pio, s->sm, s->offset, &c) == PICO_OK);
}

static void arm_sm(direct_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
}

static int start_response_dma(direct_sm_t *s) {
    const int ch = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(s->pio, s->sm, true));
    dma_channel_configure((uint)ch, &cfg, &s->pio->txf[s->sm],
                          g_response_words, DIRECT_SERVICE_CYCLES, true);
    return ch;
}

static uint32_t dma_remaining(int ch) {
    return dma_channel_hw_addr((uint)ch)->transfer_count & 0x0FFFFFFFu;
}

static bool wait_tx_primed(const direct_sm_t *s) {
    const uint64_t deadline = time_us_64() + 200u;
    while (time_us_64() <= deadline) {
        if (pio_sm_get_tx_fifo_level(s->pio, s->sm) > 0u) return true;
        tight_loop_contents();
    }
    return false;
}

static bool self_loop_seen(const direct_result_t *r) {
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

    g_response_words[0] = encode_word_direct(0xFEEBu);
    for (uint i = 1u; i < DIRECT_SERVICE_CYCLES; ++i)
        g_response_words[i] = encode_word_direct(0x9090u);

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B PIO-Direct Post-Reset Test - 0.300 MHz\n");
    printf("RESET qualification: clock-only; all response/observer SMs disabled\n");
    printf("Measurement epoch  : arm after RESET clocks with CLK stopped LOW\n");
    printf("Timing             : AF-H2; PIO1 owns data and PINDIRS\n");
    printf("Instruction        : EB FE (JMP $) at FFFF0\n");
    printf("Critical response  : DMA -> PIO1 TX FIFO -> AD GPIO/PINDIRS\n");
    printf("Observer/phase     : passive PIO0; default input synchronizers\n");
    printf("Canonical gate     : unchanged\n\n");

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    direct_sm_t responder, observer, phase;
    init_responder_sm(&responder);
    init_observer_sm(&observer);
    init_phase_sm(&phase);
    direct_result_t r = {0};

    set_intr(false);
    hold_reset(true);
    release_ad();
    perf_clock_start(&clock);
    r.reset_ok = wait_reset_clocks(RESET_CLOCKS);
    perf_clock_stop(&clock);

    arm_sm(&responder);
    arm_sm(&observer);
    arm_sm(&phase);
    route_ad_to_responder(&responder);
    const int dma_ch = start_response_dma(&responder);
    r.tx_primed = wait_tx_primed(&responder);
    r.pre_dma = dma_remaining(dma_ch);
    r.pre_release_clean = r.pre_dma > 0u &&
                          pio_sm_is_rx_fifo_empty(observer.pio, observer.sm) &&
                          pio_sm_is_rx_fifo_empty(phase.pio, phase.sm);

    pio_enable_sm_mask_in_sync(pio0, (1u << observer.sm) | (1u << phase.sm));
    pio_sm_set_enabled(responder.pio, responder.sm, true);

    if (r.reset_ok && r.tx_primed && r.pre_release_clean) {
        hold_reset(false);
        perf_clock_start(&clock);
        const uint64_t deadline = time_us_64() + timeout_us_from_clocks(DIRECT_TIMEOUT_CLOCKS);
        while (time_us_64() <= deadline && r.observed_count < DIRECT_TRACE_DEPTH) {
            while (!pio_sm_is_rx_fifo_empty(observer.pio, observer.sm) &&
                   r.observed_count < DIRECT_TRACE_DEPTH) {
                r.observed[r.observed_count++] = pio_sm_get(observer.pio, observer.sm);
            }
            if (self_loop_seen(&r) && r.observed_count >= 8u) break;
            tight_loop_contents();
        }
    }

    hold_reset(true);
    perf_clock_stop(&clock);
    pio_sm_set_enabled(responder.pio, responder.sm, false);
    pio_sm_exec(responder.pio, responder.sm, pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_set_enabled(observer.pio, observer.sm, false);
    pio_sm_set_enabled(phase.pio, phase.sm, false);
    dma_channel_abort((uint)dma_ch);

    while (!pio_sm_is_rx_fifo_empty(observer.pio, observer.sm) &&
           r.observed_count < DIRECT_TRACE_DEPTH)
        r.observed[r.observed_count++] = pio_sm_get(observer.pio, observer.sm);
    while (!pio_sm_is_rx_fifo_empty(phase.pio, phase.sm) &&
           r.phase_count < DIRECT_PHASE_COUNT)
        r.phase_raw[r.phase_count++] = pio_sm_get(phase.pio, phase.sm);

    r.post_dma = dma_remaining(dma_ch);
    r.first_ale_ok = r.observed_count > 0u && decode_address(r.observed[0]) == 0xFFFF0u;
    r.dma_complete = r.post_dma == 0u;
    r.self_loop = self_loop_seen(&r);

    route_ad_to_sio();
    release_ad();
    set_intr(false);

    printf("RESET clock count      = %s\n", r.reset_ok ? "PASS" : "FAIL");
    printf("TX FIFO primed         = %s\n", r.tx_primed ? "PASS" : "FAIL");
    printf("PRE-RELEASE DMA remain = %lu\n", (unsigned long)r.pre_dma);
    printf("PRE-RESET EVENT LEAK   = %s\n", r.pre_release_clean ? "NO" : "YES / INVALID");
    printf("FIRST post-reset ALE   = %s", r.first_ale_ok ? "FFFF0 PASS" : "FAIL");
    if (r.observed_count > 0u)
        printf(" (observed %05lX)", (unsigned long)decode_address(r.observed[0]));
    printf("\n\nALE trace (%u):\n", r.observed_count);
    for (uint i = 0u; i < r.observed_count; ++i)
        printf("  %02u = %05lX\n", i, (unsigned long)decode_address(r.observed[i]));

    static const char *const phase_names[DIRECT_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    printf("\n[POST-RESET FIRST-CYCLE GPIO SNAPSHOTS]\n");
    printf("phase raw_gpio  ALE CLK IOM DTR BHE AD16\n");
    for (uint i = 0u; i < r.phase_count; ++i) {
        const uint32_t raw = r.phase_raw[i];
        printf("%-4s  %08lX   %u   %u   %u   %u   %u  %04X\n",
               phase_names[i], (unsigned long)raw,
               (unsigned)((raw >> V30_PIN_ALE) & 1u),
               (unsigned)((raw >> V30_PIN_CLK) & 1u),
               (unsigned)((raw >> V30_PIN_IOM) & 1u),
               (unsigned)((raw >> V30_PIN_DTR) & 1u),
               (unsigned)((raw >> V30_PIN_BHE) & 1u),
               (unsigned)raw_to_ad_direct(raw));
    }
    printf("Expected response word = FEEB (EB FE / JMP $)\n");
    printf("\nPOST-RUN DMA remain    = %lu\n", (unsigned long)r.post_dma);
    printf("DMA -> PIO completion  = %s\n", r.dma_complete ? "PASS" : "FAIL");
    printf("SELF-LOOP discriminator= %s\n", r.self_loop ? "PASS" : "FAIL");

    const bool valid = r.reset_ok && r.tx_primed && r.pre_release_clean && r.first_ale_ok;
    const bool pass = valid && r.dma_complete && r.self_loop;
    printf("MEASUREMENT EPOCH      = %s\n", valid ? "VALID" : "INVALID");
    printf("PC1-B PIO-DIRECT RESULT= %s\n", pass ? "PASS" : (valid ? "FAIL" : "INVALID"));
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    dma_channel_unclaim((uint)dma_ch);
    while (true) tight_loop_contents();
}
