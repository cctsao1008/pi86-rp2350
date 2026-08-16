/*
 * PC1-A Rev1 reset-vector hot-pipeline diagnostic at 0.300 MHz.
 *
 * Purpose:
 *   Test whether Cortex-M33 software polling can sustain back-to-back V30
 *   instruction reads when all per-cycle decode, logging, scoring, address
 *   checking, and helper-return overhead are removed from the service path.
 *
 * The test uses the D2-H2 electrical candidate found by timing matrix v3 and
 * services the three known reset-vector words in a single unrolled hot path:
 *
 *   FFFF0h -> 00EAh  (EA 00)
 *   FFFF2h -> 0000h  (00 00)
 *   FFFF4h -> 90F0h  (F0 90)
 *
 * For each word the hot path does only:
 *   ALE-high latch -> ALE fall -> next fresh CLK fall (D2) -> drive word ->
 *   two subsequent fresh CLK falls (H2) -> release AD -> immediately reacquire
 *   the next ALE window.
 *
 * No address decode, expected-address comparison, pad readback, printf, generic
 * memory lookup, PIC/PIT work, or result scoring occurs between the three
 * services. A passive PIO observer independently records ALE addresses.
 *
 * PASS requires FFFF0 -> FFFF2 -> FFFF4 -> F0000. If the hot pipeline still
 * cannot sustain the sequence, that is strong evidence that per-cycle M33
 * polling is not a viable continuous-bus service architecture at 0.300 MHz and
 * phase-critical transaction handling should move into PIO/hardware timing.
 *
 * Canonical PC1-A Rev1 remains unchanged.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "perf_ale_observer.pio.h"

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} hp_observer_t;

typedef struct {
    bool reset_ok;
    bool service_ok[3];
    uint32_t m33_t1[3];
    uint32_t observer_t1[6];
    uint observer_count;
    bool pass;
} hp_result_t;

static const uint16_t hp_words[3] = {
    0x00EAu,
    0x0000u,
    0x90F0u,
};

static void hp_observer_init(hp_observer_t *o) {
    o->pio = pio1;
    o->sm = pio_claim_unused_sm(o->pio, true);
    o->offset = pio_add_program(o->pio, &perf_ale_observer_program);

    pio_sm_config c = perf_ale_observer_program_get_default_config(o->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(o->pio, o->sm, o->offset, &c);
}

static void hp_observer_start(hp_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
    pio_sm_clear_fifos(o->pio, o->sm);
    pio_sm_restart(o->pio, o->sm);
    pio_sm_exec(o->pio, o->sm, pio_encode_jmp(o->offset));
    pio_sm_set_enabled(o->pio, o->sm, true);
}

static void hp_observer_stop(hp_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
}

static inline bool __not_in_flash_func(hp_wait_fresh_fall)(void) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_CLK, true, time_us_64() + t, NULL)) return false;
    return wait_level_until(V30_PIN_CLK, false, time_us_64() + t, NULL);
}

/*
 * Capture one ALE-high address window and return immediately after ALE falls.
 * The raw T1 snapshot is stored for post-run diagnosis only; no decode occurs
 * in the service pipeline.
 */
static inline bool __not_in_flash_func(hp_capture_ale_fall)(uint32_t *last_high) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    uint32_t s = 0u;

    if (!wait_level_until(V30_PIN_ALE, false, time_us_64() + t, NULL)) return false;
    if (!wait_level_until(V30_PIN_ALE, true, time_us_64() + t, &s)) return false;

    uint32_t last = s;
    while (sample_bit(s, V30_PIN_ALE) != 0u) {
        last = s;
        s = sio_hw->gpio_in;
    }
    *last_high = last;
    return true;
}

/*
 * D2-H2 service primitive with deliberately minimal work. The caller has just
 * observed ALE fall. There is no logging or pad readback here.
 */
static inline bool __not_in_flash_func(hp_drive_d2h2)(uint16_t value) {
    if (!hp_wait_fresh_fall()) return false;  /* D2 start. */

    drive_data(value, V30_BUS_LANES_WORD);

    if (!hp_wait_fresh_fall()) {
        release_ad();
        return false;
    }
    if (!hp_wait_fresh_fall()) {
        release_ad();
        return false;
    }

    release_ad();
    return true;
}

static void __not_in_flash_func(hp_run)(perf_clock_t *clock,
                                        hp_observer_t *observer,
                                        hp_result_t *r) {
    *r = (hp_result_t){0};

    release_ad();
    set_intr(false);
    hold_reset(true);
    hp_observer_start(observer);
    perf_clock_start(clock);

    r->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (!r->reset_ok) goto done;

    hold_reset(false);

    /*
     * Fully unrolled hot pipeline. Do not insert address decode, validation,
     * logging, or scoring between these blocks.
     */
    if (!hp_capture_ale_fall(&r->m33_t1[0])) goto done;
    if (!hp_drive_d2h2(hp_words[0])) goto done;
    r->service_ok[0] = true;

    if (!hp_capture_ale_fall(&r->m33_t1[1])) goto done;
    if (!hp_drive_d2h2(hp_words[1])) goto done;
    r->service_ok[1] = true;

    if (!hp_capture_ale_fall(&r->m33_t1[2])) goto done;
    if (!hp_drive_d2h2(hp_words[2])) goto done;
    r->service_ok[2] = true;

    /* Wait only for passive observer evidence of the far-jump destination. */
    {
        const uint64_t deadline = time_us_64() + timeout_us_from_clocks(48u);
        while (pio_sm_get_rx_fifo_level(observer->pio, observer->sm) < 4u &&
               time_us_64() <= deadline) {
            tight_loop_contents();
        }
    }

done:
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);
    hp_observer_stop(observer);

    while (!pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
           r->observer_count < 6u) {
        r->observer_t1[r->observer_count++] = pio_sm_get(observer->pio, observer->sm);
    }

    r->pass = r->reset_ok &&
              r->service_ok[0] && r->service_ok[1] && r->service_ok[2] &&
              r->observer_count >= 4u &&
              decode_address(r->observer_t1[0]) == 0xFFFF0u &&
              decode_address(r->observer_t1[1]) == 0xFFFF2u &&
              decode_address(r->observer_t1[2]) == 0xFFFF4u &&
              decode_address(r->observer_t1[3]) == 0xF0000u;
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 reset-vector hot pipeline - 0.300 MHz\n");
    printf("Read timing       : D2 start / H2 hold\n");
    printf("Service path      : three reset-vector reads, fully unrolled\n");
    printf("Hot-path work     : ALE latch + edge waits + AD drive/release only\n");
    printf("Removed hot work  : decode/check/log/pad-read/memory/PIC/PIT\n");
    printf("Reset-vector code : EA 00 00 00 F0 90\n");
    printf("Observer          : passive PIO ALE latch\n");
    printf("PASS discriminator: FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("Canonical gate    : unchanged\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);
    hp_observer_t observer;
    hp_observer_init(&observer);
    hp_result_t result;

    const uint32_t irq_state = save_and_disable_interrupts();
    hp_run(&clock, &observer, &result);
    restore_interrupts(irq_state);

    printf("RESET clock count = %s\n", result.reset_ok ? "PASS" : "FAIL");
    printf("M33 service flags = %s / %s / %s\n",
           result.service_ok[0] ? "OK" : "MISS",
           result.service_ok[1] ? "OK" : "MISS",
           result.service_ok[2] ? "OK" : "MISS");
    printf("M33 raw T1 decode = %05lX / %05lX / %05lX\n",
           (unsigned long)decode_address(result.m33_t1[0]),
           (unsigned long)decode_address(result.m33_t1[1]),
           (unsigned long)decode_address(result.m33_t1[2]));

    printf("\nPIO ALE sequence (%u captured):\n", result.observer_count);
    for (uint i = 0u; i < result.observer_count; ++i) {
        printf("  PIO%u = %05lX\n",
               i,
               (unsigned long)decode_address(result.observer_t1[i]));
    }

    printf("\nExpected control-flow sequence = FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("RESET VECTOR HOT PIPELINE      = %s\n", result.pass ? "PASS" : "FAIL");
    if (!result.pass) {
        printf("Architecture discriminator     = if FFFF2/FFFF4 are still missed or polluted,\n");
        printf("                                 per-cycle M33 polling is not yet sustainable\n");
        printf("                                 at 0.300 MHz with this D2-H2 transaction model.\n");
    }
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
