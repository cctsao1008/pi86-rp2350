/*
 * Performance Characterization 1A — continuous software-polling baseline.
 *
 * Reuse the current continuous-clock harness implementation without changing
 * the original 1–8 MHz target.  Rename its main() while including the source,
 * then provide a diagnostic main() that extends the sweep below 1 MHz.
 *
 * This target exists to answer one narrow question before the PIO-timed bus
 * engine is introduced: at what free-running V30 clock, if any, does the
 * Cortex-M33 software-polling service path become reliably functional?
 */
#define main performance_characterization_1_original_main
#include "performance_characterization_1.c"
#undef main

static const uint32_t polling_baseline_frequency_points_hz[] = {
    100000u,
    200000u,
    300000u,
    500000u,
    750000u,
    1000000u,
    2000000u,
    2500000u,
    3000000u,
    4000000u,
    4770000u,
    6000000u,
    8000000u,
};

int main(void) {
    prepare_header_high_z();
    init_data_path();
    hold_reset(true);
    set_intr(false);
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPerformance Characterization 1A - Continuous Software-Polling Baseline\n");
    printf("One firmware run sweeps 13 configured clock points from 0.100 to 8.000 MHz.\n");
    printf("Purpose: establish the polling architecture baseline before PIO-timed bus-engine work.\n");
    printf("No per-bus-cycle USB logging occurs while a point is running.\n");
    printf("Configured clock is not claimed as independently measured physical frequency.\n");
    printf("Gate 12 stepped-clock firmware remains the last-known-good functional regression baseline.\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    const uint point_count = (uint)(sizeof(polling_baseline_frequency_points_hz) /
                                    sizeof(polling_baseline_frequency_points_hz[0]));
    perf_result_t results[sizeof(polling_baseline_frequency_points_hz) /
                          sizeof(polling_baseline_frequency_points_hz[0])];

    int last_good = -1;
    int first_fail = -1;

    for (uint i = 0u; i < point_count; ++i) {
        printf("\n>>> PC1-A polling baseline point %u/%u: ", i + 1u, point_count);
        print_mhz(polling_baseline_frequency_points_hz[i]);
        printf(" configured V30 clock <<<\n");
        fflush(stdout);

        (void)run_frequency_point(&clock,
                                  polling_baseline_frequency_points_hz[i],
                                  &results[i]);
        print_point_result(&results[i], i, point_count);

        if (results[i].pass)
            last_good = (int)i;
        else if (first_fail < 0)
            first_fail = (int)i;

        sleep_ms(100);
    }

    hold_reset(true);
    set_intr(false);
    release_ad();
    perf_clock_stop(&clock);

    printf("\n============================================================\n");
    printf("PERFORMANCE CHARACTERIZATION 1A - POLLING BASELINE SUMMARY\n");
    printf("============================================================\n");
    for (uint i = 0u; i < point_count; ++i) {
        print_mhz(results[i].configured_hz);
        printf("   %s", results[i].pass ? "PASS" : "FAIL");
        if (!results[i].pass)
            printf("   (%s)", fail_reason_name(results[i].fail_reason));
        printf("\n");
    }

    printf("\nLast known-good polling clock : ");
    if (last_good >= 0) print_mhz(results[last_good].configured_hz);
    else printf("none");
    printf("\nFirst failing polling clock    : ");
    if (first_fail >= 0) print_mhz(results[first_fail].configured_hz);
    else printf("none in sweep");
    printf("\n");
    printf("Interpret this as a software-polling architecture baseline, not the RP2350 final performance ceiling.\n");
    printf("PC1-B will move critical bus-phase ownership into PIO and rerun comparable characterization.\n");
    printf("Physical CLK must be scope/counter verified before configured values are called measured frequencies.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
