/*
 * PC1-B minimal PIO-triggered SIO reset-vector prototype at 0.300 MHz.
 *
 * Architecture discriminator:
 * - PIO0 SM: continuous free-running V30 clock (existing generator).
 * - PIO1 SM0: deterministic D2/H2 event generator from ALE/CLK.
 * - PIO1 SM1: passive ALE address observer.
 * - M33 IRQ handler: drives/releases the non-contiguous AD15:0 bus with SIO.
 *
 * The M33 no longer polls ALE or CLK for each transaction.  PIO IRQ0 means
 * "D2 start" and PIO IRQ1 means "H2 release".  The first three D2 events drive
 * the reset-vector words 00EA, 0000, and 90F0.  After the third release the AD
 * bus remains high-Z.
 *
 * PASS requires the passive observer to see:
 *   FFFF0 -> FFFF2 -> FFFF4 -> F0000
 *
 * This prototype intentionally keeps the existing SIO data-path helper so the
 * experiment isolates cycle/phase acquisition from the PC1-A polling model.
 * A failure does not by itself prove that a more direct/precomputed SIO or DMA
 * response cannot meet the window.
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
} pc1b_sm_t;

static volatile uint g_service_index;
static volatile uint g_start_irq_count;
static volatile uint g_release_irq_count;
static volatile uint g_coalesced_irq_count;
static volatile bool g_drive_active;
static volatile bool g_service_overrun;

static const uint16_t g_reset_words[3] = {
    0x00EAu,
    0x0000u,
    0x90F0u,
};

static void __not_in_flash_func(pc1b_pio1_irq0_handler)(void) {
    const bool start_pending = pio_interrupt_get(pio1, 0u);
    const bool release_pending = pio_interrupt_get(pio1, 1u);

    /*
     * Both flags pending at ISR entry means M33 did not service the D2 event
     * before PIO reached H2.  Treat this as an explicit latency failure rather
     * than manufacturing a misleading drive/release pulse in software.
     */
    if (start_pending && release_pending) {
        ++g_coalesced_irq_count;
        g_service_overrun = true;
        pio_interrupt_clear(pio1, 0u);
        pio_interrupt_clear(pio1, 1u);
        release_ad();
        g_drive_active = false;
        return;
    }

    if (start_pending) {
        pio_interrupt_clear(pio1, 0u);
        ++g_start_irq_count;

        if (g_service_index < 3u) {
            if (g_drive_active) {
                g_service_overrun = true;
                release_ad();
            }
            drive_data(g_reset_words[g_service_index], V30_BUS_LANES_WORD);
            g_drive_active = true;
        }
    }

    if (release_pending) {
        pio_interrupt_clear(pio1, 1u);
        ++g_release_irq_count;

        if (g_drive_active) {
            release_ad();
            g_drive_active = false;
            if (g_service_index < 3u) ++g_service_index;
        } else if (g_service_index < 3u) {
            g_service_overrun = true;
        }
    }
}

static void pc1b_event_sm_init(pc1b_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &pc1b_d2h2_events_program);

    pio_sm_config c = pc1b_d2h2_events_program_get_default_config(s->offset);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_observer_sm_init(pc1b_sm_t *s) {
    s->pio = pio1;
    s->sm = pio_claim_unused_sm(s->pio, true);
    s->offset = pio_add_program(s->pio, &perf_ale_observer_program);

    pio_sm_config c = perf_ale_observer_program_get_default_config(s->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(s->pio, s->sm, s->offset, &c);
}

static void pc1b_sm_start(pc1b_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
    pio_sm_clear_fifos(s->pio, s->sm);
    pio_sm_restart(s->pio, s->sm);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(s->offset));
    pio_sm_set_enabled(s->pio, s->sm, true);
}

static void pc1b_sm_stop(pc1b_sm_t *s) {
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void pc1b_irq_init(void) {
    pio_interrupt_clear(pio1, 0u);
    pio_interrupt_clear(pio1, 1u);

    pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);
    pio_set_irq0_source_enabled(pio1, pis_interrupt1, true);

    irq_set_exclusive_handler(PIO1_IRQ_0, pc1b_pio1_irq0_handler);
    irq_set_priority(PIO1_IRQ_0, 0u);
    irq_set_enabled(PIO1_IRQ_0, true);
}

static void pc1b_irq_deinit(void) {
    irq_set_enabled(PIO1_IRQ_0, false);
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, false);
    pio_set_irq0_source_enabled(pio1, pis_interrupt1, false);
    pio_interrupt_clear(pio1, 0u);
    pio_interrupt_clear(pio1, 1u);
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-B PIO-triggered SIO reset-vector prototype - 0.300 MHz\n");
    printf("Clock             : continuous PIO free-run\n");
    printf("Timing front-end  : PIO1 D2/H2 event state machine\n");
    printf("Data response     : M33 IRQ -> existing SIO AD drive/release\n");
    printf("M33 ALE/CLK poll  : disabled in per-cycle service path\n");
    printf("Reset-vector code : EA 00 00 00 F0 90\n");
    printf("Observer          : independent passive PIO ALE latch\n");
    printf("PASS discriminator: FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("Canonical gate    : unchanged\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    pc1b_sm_t event_sm;
    pc1b_sm_t observer_sm;
    pc1b_event_sm_init(&event_sm);
    pc1b_observer_sm_init(&observer_sm);

    g_service_index = 0u;
    g_start_irq_count = 0u;
    g_release_irq_count = 0u;
    g_coalesced_irq_count = 0u;
    g_drive_active = false;
    g_service_overrun = false;

    set_intr(false);
    hold_reset(true);
    release_ad();

    pc1b_irq_init();
    pc1b_sm_start(&observer_sm);
    pc1b_sm_start(&event_sm);
    perf_clock_start(&clock);

    const bool reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (reset_ok) hold_reset(false);

    /*
     * Let the hardware front-end and IRQ service run without foreground bus
     * work.  The passive observer needs four ALE windows to prove the far jump.
     */
    const uint64_t deadline = time_us_64() + timeout_us_from_clocks(96u);
    while (time_us_64() <= deadline &&
           pio_sm_get_rx_fifo_level(observer_sm.pio, observer_sm.sm) < 4u &&
           !g_service_overrun) {
        tight_loop_contents();
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    pc1b_sm_stop(&event_sm);
    pc1b_sm_stop(&observer_sm);
    pc1b_irq_deinit();

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

    const bool irq_accounting_ok = g_service_index >= 3u &&
                                   g_start_irq_count >= 3u &&
                                   g_release_irq_count >= 3u &&
                                   g_coalesced_irq_count == 0u &&
                                   !g_service_overrun;

    const bool pass = reset_ok && flow_ok && irq_accounting_ok;

    printf("RESET clock count = %s\n", reset_ok ? "PASS" : "FAIL");
    printf("PIO IRQ starts    = %u\n", (unsigned)g_start_irq_count);
    printf("PIO IRQ releases  = %u\n", (unsigned)g_release_irq_count);
    printf("Service words     = %u / 3\n", (unsigned)g_service_index);
    printf("Coalesced IRQs    = %u\n", (unsigned)g_coalesced_irq_count);
    printf("Service overrun   = %s\n", g_service_overrun ? "YES" : "NO");

    printf("\nPIO ALE sequence (%u captured):\n", observed_count);
    for (uint i = 0u; i < observed_count; ++i) {
        printf("  PIO%u = %05lX\n",
               i,
               (unsigned long)decode_address(observed[i]));
    }

    printf("\nExpected control-flow sequence = FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("PC1-B PIO->M33/SIO RESULT      = %s\n", pass ? "PASS" : "FAIL");
    if (!pass) {
        printf("Interpretation                    = this tests PIO event acquisition plus\n");
        printf("                                    M33 IRQ/SIO response latency; it does not\n");
        printf("                                    yet test DMA or fully PIO-driven data.\n");
    }
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
