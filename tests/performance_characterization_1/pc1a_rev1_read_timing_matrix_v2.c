/*
 * PC1-A Rev1 read-response timing matrix v2 at 0.300 MHz.
 *
 * Improvements over v1:
 * - drive timing is taken directly from the ALE-fall hot path;
 * - no decode/validation work is done before D0 drive;
 * - D1-D4 are explicit fresh CLK transitions after ALE falls;
 * - a second PIO SM passively captures the last GPIO snapshot while ALE is high,
 *   so "next ALE" cannot be skipped by the M33 observer;
 * - SIO output latch, OE mask, and pad readback are reported separately.
 *
 * The passive observer never drives the V30 bus. Canonical Rev1 is unchanged.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#include "perf_ale_observer.pio.h"

typedef enum {
    V2_HIZ = 0,
    V2_D0_IMMEDIATE,
    V2_D1_NEXT_RISE,
    V2_D2_NEXT_FALL,
    V2_D3_SECOND_RISE,
    V2_D4_SECOND_FALL,
} v2_phase_t;

typedef struct {
    const char *name;
    v2_phase_t phase;
    uint16_t value;
    bool drive;
    bool functional;
} v2_case_t;

typedef struct {
    bool reset_ok;
    bool first_seen;
    bool phase_ok;
    bool hold_ok;
    bool observer_ok;
    uint32_t first_t1_m33;
    uint32_t observer_t1[4];
    uint observer_count;
    uint16_t pre_pad;
    uint16_t out_latch;
    uint16_t pad;
    uint32_t oe;
    uint8_t clk_at_drive;
    uint8_t ale_at_drive;
} v2_result_t;

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} ale_observer_t;

static const v2_case_t v2_cases[] = {
    {"HIZ",      V2_HIZ,          0x0000u, false, true},
    {"D0",       V2_D0_IMMEDIATE, 0x00EAu, true,  true},
    {"D1",       V2_D1_NEXT_RISE, 0x00EAu, true,  true},
    {"D2",       V2_D2_NEXT_FALL, 0x00EAu, true,  true},
    {"D3",       V2_D3_SECOND_RISE,0x00EAu,true,  true},
    {"D4",       V2_D4_SECOND_FALL,0x00EAu,true,  true},
    {"D0-A55A",  V2_D0_IMMEDIATE, 0xA55Au, true,  false},
    {"HIZ-POST", V2_HIZ,          0x0000u, false, true},
};

static void ale_observer_init(ale_observer_t *o) {
    o->pio = pio1;
    o->sm = pio_claim_unused_sm(o->pio, true);
    o->offset = pio_add_program(o->pio, &perf_ale_observer_program);

    pio_sm_config c = perf_ale_observer_program_get_default_config(o->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(o->pio, o->sm, o->offset, &c);
}

static void ale_observer_start(ale_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
    pio_sm_clear_fifos(o->pio, o->sm);
    pio_sm_restart(o->pio, o->sm);
    pio_sm_exec(o->pio, o->sm, pio_encode_jmp(o->offset));
    pio_sm_set_enabled(o->pio, o->sm, true);
}

static void ale_observer_stop(ale_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
}

static bool __not_in_flash_func(wait_fresh_rise)(void) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_CLK, false, time_us_64() + t, NULL)) return false;
    return wait_level_until(V30_PIN_CLK, true, time_us_64() + t, NULL);
}

static bool __not_in_flash_func(wait_fresh_fall)(void) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_CLK, true, time_us_64() + t, NULL)) return false;
    return wait_level_until(V30_PIN_CLK, false, time_us_64() + t, NULL);
}

static bool __not_in_flash_func(advance_v2_phase)(v2_phase_t phase) {
    switch (phase) {
        case V2_HIZ:
        case V2_D0_IMMEDIATE:
            return true;
        case V2_D1_NEXT_RISE:
            return wait_fresh_rise();
        case V2_D2_NEXT_FALL:
            return wait_fresh_fall();
        case V2_D3_SECOND_RISE:
            if (!wait_fresh_rise()) return false;
            return wait_fresh_rise();
        case V2_D4_SECOND_FALL:
            if (!wait_fresh_fall()) return false;
            return wait_fresh_fall();
    }
    return false;
}

static bool __not_in_flash_func(wait_first_ale_fall_hot)(uint32_t *last_high) {
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

static void __not_in_flash_func(run_v2_case)(perf_clock_t *clock,
                                              ale_observer_t *observer,
                                              const v2_case_t *tc,
                                              v2_result_t *r) {
    *r = (v2_result_t){0};
    release_ad();
    set_intr(false);
    hold_reset(true);
    ale_observer_start(observer);
    perf_clock_start(clock);

    r->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (!r->reset_ok) goto done;
    hold_reset(false);

    /* Hot path: no address decode, lane decode, validation, or logging before D0. */
    if (!wait_first_ale_fall_hot(&r->first_t1_m33)) goto done;
    r->first_seen = true;
    r->pre_pad = decode_ad(sio_hw->gpio_in);

    r->phase_ok = advance_v2_phase(tc->phase);
    if (!r->phase_ok) goto done;

    if (tc->drive) {
        drive_data(tc->value, V30_BUS_LANES_WORD);
        const uint32_t s = sio_hw->gpio_in;
        r->out_latch = decode_ad(sio_hw->gpio_out);
        r->pad = decode_ad(s);
        r->oe = sio_hw->gpio_oe & V30_AD_BUS_MASK;
        r->clk_at_drive = (uint8_t)sample_bit(s, V30_PIN_CLK);
        r->ale_at_drive = (uint8_t)sample_bit(s, V30_PIN_ALE);
        r->hold_ok = wait_falling_edges(1u);
        release_ad();
        if (!r->hold_ok) goto done;
    } else {
        const uint32_t s = sio_hw->gpio_in;
        r->out_latch = decode_ad(sio_hw->gpio_out);
        r->pad = decode_ad(s);
        r->oe = sio_hw->gpio_oe & V30_AD_BUS_MASK;
        r->clk_at_drive = (uint8_t)sample_bit(s, V30_PIN_CLK);
        r->ale_at_drive = (uint8_t)sample_bit(s, V30_PIN_ALE);
        r->hold_ok = true;
    }

    {
        const uint64_t deadline = time_us_64() + timeout_us_from_clocks(24u);
        while (pio_sm_get_rx_fifo_level(observer->pio, observer->sm) < 2u &&
               time_us_64() <= deadline) {
            tight_loop_contents();
        }
        r->observer_ok = pio_sm_get_rx_fifo_level(observer->pio, observer->sm) >= 2u;
    }

done:
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);
    ale_observer_stop(observer);

    while (!pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) && r->observer_count < 4u)
        r->observer_t1[r->observer_count++] = pio_sm_get(observer->pio, observer->sm);
}

static const char *v2_result_name(const v2_case_t *tc, const v2_result_t *r) {
    if (!tc->functional) return "ELEC";
    if (!r->reset_ok || !r->first_seen || !r->phase_ok || !r->hold_ok || !r->observer_ok)
        return "FAIL";
    if (r->observer_count < 2u) return "FAIL";
    if (decode_address(r->observer_t1[0]) != 0xFFFF0u) return "FAIL";
    if (decode_address(r->observer_t1[1]) != 0xFFFF2u) return "FAIL";
    if (!tc->drive) return "PASS";
    if (r->out_latch != tc->value) return "FAIL";
    if (r->pad != tc->value) return "FAIL";
    return "PASS";
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 read-response timing matrix v2 - 0.300 MHz\n");
    printf("Observer         : passive PIO ALE latch, independent of M33 next-cycle polling\n");
    printf("D0 hot path      : drive begins immediately after M33 detects ALE falling\n");
    printf("No pre-drive work: no decode/validation/logging before phase action\n");
    printf("Release rule     : one subsequent falling edge after drive\n");
    printf("Canonical gate   : unchanged\n\n");
    printf("Phase definitions after ALE-fall detection:\n");
    printf("  D0 = immediate\n");
    printf("  D1 = next fresh CLK rising edge\n");
    printf("  D2 = next fresh CLK falling edge\n");
    printf("  D3 = second fresh CLK rising edge\n");
    printf("  D4 = second fresh CLK falling edge\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);
    ale_observer_t observer;
    ale_observer_init(&observer);

    v2_result_t results[sizeof(v2_cases) / sizeof(v2_cases[0])];
    const uint32_t irq_state = save_and_disable_interrupts();
    for (uint i = 0u; i < sizeof(v2_cases) / sizeof(v2_cases[0]); ++i)
        run_v2_case(&clock, &observer, &v2_cases[i], &results[i]);
    restore_interrupts(irq_state);

    printf("case      M33first pre   value  out   pad   OE-mask   C A  PIO0   PIO1   PIO2   result\n");
    for (uint i = 0u; i < sizeof(v2_cases) / sizeof(v2_cases[0]); ++i) {
        const v2_case_t *tc = &v2_cases[i];
        const v2_result_t *r = &results[i];
        const uint32_t p0 = r->observer_count > 0u ? decode_address(r->observer_t1[0]) : 0u;
        const uint32_t p1 = r->observer_count > 1u ? decode_address(r->observer_t1[1]) : 0u;
        const uint32_t p2 = r->observer_count > 2u ? decode_address(r->observer_t1[2]) : 0u;
        printf("%-9s %05lX  %04X  %04X  %04X  %04X  %08lX  %u %u  %05lX  %05lX  %05lX  %s\n",
               tc->name,
               (unsigned long)decode_address(r->first_t1_m33),
               (unsigned)r->pre_pad,
               (unsigned)tc->value,
               (unsigned)r->out_latch,
               (unsigned)r->pad,
               (unsigned long)r->oe,
               (unsigned)r->clk_at_drive,
               (unsigned)r->ale_at_drive,
               (unsigned long)p0,
               (unsigned long)p1,
               (unsigned long)p2,
               v2_result_name(tc, r));
    }

    printf("\nFunctional PASS requires PIO-observed FFFF0 -> FFFF2, and for driven rows\n");
    printf("both SIO output latch and pad readback must equal the requested 00EA word.\n");
    printf("A55A remains electrical-only. PIO observer is passive and never drives AD.\n");
    printf("This remains a coarse phase experiment, not ns-level timing characterization.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
