/*
 * PC1-B direct-SIO reset-vector prototype at 0.300 MHz.
 *
 * This is the next discriminator after the first PIO-triggered SIO prototype:
 * - PIO still owns deterministic D2/H2 timing.
 * - Start and release events use separate PIO CPU IRQ lines.
 * - Reset-vector GPIO bit patterns are precomputed before RESET release.
 * - Critical ISR entry performs raw SIO CLR/SET/OE writes before accounting.
 * - No drive_data() helper, LUT lookup, ALE/CLK polling, or combined pending
 *   branch exists in the critical response path.
 *
 * PASS remains passive-PIO observation of:
 *   FFFF0 -> FFFF2 -> FFFF4 -> F0000
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "hardware/irq.h"
#include "pc1b_d2h2_events.pio.h"
#include "perf_ale_observer.pio.h"

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} pc1b_direct_sm_t;

static volatile uint g_direct_index;
static volatile uint g_direct_start_count;
static volatile uint g_direct_release_count;
static volatile bool g_direct_overrun;
static volatile bool g_direct_active;
static uint32_t g_direct_encoded[3];

static const uint16_t g_direct_words[3] = {
    0x00EAu,
    0x0000u,
    0x90F0u,
};

static uint32_t pc1b_encode_word(uint16_t value) {
    return data_lo_lut[value & 0xFFu] |
           data_hi_lut[(value >> 8) & 0xFFu];
}

static void __not_in_flash_func(pc1b_start_irq_handler)(void) {
    /* Critical path first: acknowledge, then raw SIO response. */
    pio_interrupt_clear(pio1, 0u);

    const uint idx = g_direct_index;
    if (idx < 3u) {
        sio_hw->gpio_clr = V30_AD_BUS_MASK;
        sio_hw->gpio_set = g_direct_encoded[idx];
        sio_hw->gpio_oe_set = V30_AD_BUS_MASK;
        g_direct_active = true;
    }

    ++g_direct_start_count;
}

static void __not_in_flash_func(pc1b_release_irq_handler)(void) {
    /* Critical path first: drop AD ownership immediately. */
    pio_interrupt_clear(pio1, 1u);
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;

    if (g_direct_active) {
        g_direct_active = false;
        if (g_direct_index < 3u) ++g_direct_index;
    } else if (g_direct_index < 3u) {
        g_direct_overrun = true;
    }

    ++g_direct_release_count;
}

static void pc1b_direct_event_init(pc1b_direct_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_d2h2_events_program);
    pio_sm_config c = pc1b_d2h2_events_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_direct_observer_init(pc1b_direct_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_direct_sm_start(pc1b_direct_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void pc1b_direct_sm_stop(pc1b_direct_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void pc1b_direct_irq_init(void) {
    pio_interrupt_clear(pio1, 0u);
    pio_interrupt_clear(pio1, 1u);

    pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);
    pio_set_irq1_source_enabled(pio1, pis_interrupt1, true);

    irq_set_exclusive_handler(PIO1_IRQ_0, pc1b_start_irq_handler);
    irq_set_exclusive_handler(PIO1_IRQ_1, pc1b_release_irq_handler);
    irq_set_priority(PIO1_IRQ_0, 0u);
    irq_set_priority(PIO1_IRQ_1, 0u);
    irq_set_enabled(PIO1_IRQ_0, true);
    irq_set_enabled(PIO1_IRQ_1, true);
}

static void pc1b_direct_irq_deinit(void) {
    irq_set_enabled(PIO1_IRQ_0, false);
    irq_set_enabled(PIO1_IRQ_1, false);
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, false);
    pio_set_irq1_source_enabled(pio1, pis_interrupt1, false);
    pio_interrupt_clear(pio1, 0u);
    pio_interrupt_clear(pio1, 1u);
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    for (uint i = 0u; i < 3u; ++i)
        g_direct_encoded[i] = pc1b_encode_word(g_direct_words[i]);

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B direct-SIO IRQ reset-vector prototype - 0.300 MHz\n");
    printf("Timing front-end  : PIO1 D2/H2 event state machine\n");
    printf("Start/release IRQ : separate PIO1 IRQ0 / IRQ1 handlers\n");
    printf("Data response     : precomputed raw SIO CLR/SET/OE writes\n");
    printf("M33 ALE/CLK poll  : none in per-cycle service path\n");
    printf("Reset-vector code : EA 00 00 00 F0 90\n");
    printf("PASS discriminator: FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("Canonical gate    : unchanged\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    pc1b_direct_sm_t event_sm;
    pc1b_direct_sm_t observer_sm;
    pc1b_direct_event_init(&event_sm);
    pc1b_direct_observer_init(&observer_sm);

    g_direct_index = 0u;
    g_direct_start_count = 0u;
    g_direct_release_count = 0u;
    g_direct_overrun = false;
    g_direct_active = false;

    set_intr(false);
    hold_reset(true);
    release_ad();

    pc1b_direct_irq_init();
    pc1b_direct_sm_start(&observer_sm);
    pc1b_direct_sm_start(&event_sm);
    perf_clock_start(&clock);

    const bool reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (reset_ok) hold_reset(false);

    const uint64_t deadline = time_us_64() + timeout_us_from_clocks(96u);
    while (time_us_64() <= deadline &&
           pio_sm_get_rx_fifo_level(observer_sm.pio, observer_sm.sm) < 4u &&
           !g_direct_overrun) {
        tight_loop_contents();
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    pc1b_direct_sm_stop(&event_sm);
    pc1b_direct_sm_stop(&observer_sm);
    pc1b_direct_irq_deinit();

    uint32_t observed[8] = {0};
    uint observed_count = 0u;
    while (!pio_sm_is_rx_fifo_empty(observer_sm.pio, observer_sm.sm) &&
           observed_count < 8u) {
        observed[observed_count++] = pio_sm_get(observer_sm.pio, observer_sm.sm);
    }

    const bool flow_ok = observed_count >= 4u &&
                         decode_address(observed[0]) == 0xFFFF0u &&
                         decode_address(observed[1]) == 0xFFFF2u &&
                         decode_address(observed[2]) == 0xFFFF4u &&
                         decode_address(observed[3]) == 0xF0000u;

    const bool service_ok = g_direct_index >= 3u &&
                            g_direct_start_count >= 3u &&
                            g_direct_release_count >= 3u &&
                            !g_direct_overrun;
    const bool pass = reset_ok && flow_ok && service_ok;

    printf("RESET clock count = %s\n", reset_ok ? "PASS" : "FAIL");
    printf("PIO IRQ starts    = %u\n", (unsigned)g_direct_start_count);
    printf("PIO IRQ releases  = %u\n", (unsigned)g_direct_release_count);
    printf("Service words     = %u / 3\n", (unsigned)g_direct_index);
    printf("Service overrun   = %s\n", g_direct_overrun ? "YES" : "NO");

    printf("\nPIO ALE sequence (%u captured):\n", observed_count);
    for (uint i = 0u; i < observed_count; ++i) {
        printf("  PIO%u = %05lX\n", i,
               (unsigned long)decode_address(observed[i]));
    }

    printf("\nExpected control-flow sequence = FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("PC1-B DIRECT-SIO RESULT        = %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        printf("Next discriminator              = hardware-assisted response path\n");
        printf("                                  (PIO/DMA or equivalent), not PC1-A polling.\n");
    }
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
