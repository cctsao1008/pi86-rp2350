/*
 * PC1-A Rev1 free-running bus timeline diagnostic at 0.300 MHz.
 *
 * Purpose:
 *   Observe the raw V30 bus waveform produced by the continuous clock without
 *   attempting to service memory or I/O.  This separates free-running clock /
 *   bus-phase correctness from Cortex-M33 response latency.
 *
 * The RP2350 leaves AD15:0 high-Z for the whole capture.  After RESET is held
 * for 20 counted clocks and released on a falling-edge boundary, the program
 * records every observed change on CLK/ALE/control/address-data signals into
 * SRAM and prints the trace only after RESET is asserted again and CLK stops.
 *
 * This is diagnostic only.  It does not modify the canonical PC1-A Rev1 gate.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#define TIMELINE_DEPTH       128u
#define TIMELINE_MAX_US      80u

typedef struct {
    uint32_t sequence;
    uint32_t sample;
    uint32_t elapsed_us;
} timeline_entry_t;

static inline uint32_t timeline_mask(void) {
    return V30_AD_BUS_MASK |
           (1u << V30_PIN_CLK) |
           (1u << V30_PIN_ALE) |
           (1u << V30_PIN_IOM) |
           (1u << V30_PIN_DTR) |
           (1u << V30_PIN_BHE) |
           (1u << V30_PIN_INTA) |
           (1u << V30_PIN_A16) |
           (1u << V30_PIN_A17) |
           (1u << V30_PIN_A18) |
           (1u << V30_PIN_A19);
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 free-running raw bus timeline - 0.300 MHz\n");
    printf("Clock            : continuous PIO free-run\n");
    printf("RESET            : 20 counted clocks, release after falling edge\n");
    printf("Host AD drive    : disabled for entire capture\n");
    printf("Capture          : raw GPIO transition log, no bus service\n");
    printf("Purpose          : validate free-running V30 bus phase before further polling optimization\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    timeline_entry_t trace[TIMELINE_DEPTH] = {0};
    uint captured = 0u;
    bool reset_clock_ok = false;

    const uint32_t irq_state = save_and_disable_interrupts();

    release_ad();
    set_intr(false);
    hold_reset(true);
    perf_clock_start(&clock);

    reset_clock_ok = wait_reset_clocks(RESET_CLOCKS);
    if (reset_clock_ok) {
        /* Release RESET only on the already-established falling-edge boundary. */
        hold_reset(false);

        const uint32_t mask = timeline_mask();
        const uint64_t start_us = time_us_64();
        uint32_t previous = sio_hw->gpio_in;

        trace[captured++] = (timeline_entry_t){
            .sequence = 0u,
            .sample = previous,
            .elapsed_us = 0u,
        };

        while (captured < TIMELINE_DEPTH) {
            const uint32_t sample = sio_hw->gpio_in;
            const uint64_t now_us = time_us_64();
            if ((sample & mask) != (previous & mask)) {
                trace[captured] = (timeline_entry_t){
                    .sequence = captured,
                    .sample = sample,
                    .elapsed_us = (uint32_t)(now_us - start_us),
                };
                ++captured;
                previous = sample;
            }
            if ((now_us - start_us) >= TIMELINE_MAX_US) break;
        }
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    restore_interrupts(irq_state);

    printf("RESET clock count = %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("Captured %u transition records\n\n", captured);
    printf(" seq  us  C A I D N B  A19:16  AD16   raw\n");
    printf("      |   L L O T T H\n");
    printf("      |   K E M R A E\n");
    for (uint i = 0u; i < captured; ++i) {
        const uint32_t s = trace[i].sample;
        const uint16_t ad = decode_ad(s);
        const uint8_t high = (uint8_t)((decode_address(s) >> 16) & 0x0Fu);
        printf("%4lu %3lu  %u %u %u %u %u %u    %X     %04X  %08lX\n",
               (unsigned long)trace[i].sequence,
               (unsigned long)trace[i].elapsed_us,
               (unsigned)sample_bit(s, V30_PIN_CLK),
               (unsigned)sample_bit(s, V30_PIN_ALE),
               (unsigned)sample_bit(s, V30_PIN_IOM),
               (unsigned)sample_bit(s, V30_PIN_DTR),
               (unsigned)sample_bit(s, V30_PIN_INTA),
               (unsigned)sample_bit(s, V30_PIN_BHE),
               (unsigned)high,
               (unsigned)ad,
               (unsigned long)s);
    }

    printf("\nInterpretation rules:\n");
    printf("- AD is never driven by RP2350 in this test.\n");
    printf("- The first ALE-high window should expose the reset-vector address FFFF0.\n");
    printf("- Later bus activity is not expected to execute valid code because no read data is supplied.\n");
    printf("- Use this trace to establish actual CLK/ALE/control phase relationships before moving response timing.\n");
    printf("Canonical pc1a_rev1_300khz remains unchanged.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
