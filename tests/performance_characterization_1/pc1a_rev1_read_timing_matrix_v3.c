/*
 * PC1-A Rev1 read-response timing matrix v3 at 0.300 MHz.
 *
 * This experiment narrows the v2 candidate start region to D1/D2 and sweeps
 * AD hold duration over one, two, and three subsequent falling CLK edges.
 * A passive PIO ALE observer records bus progression independently of the M33.
 *
 * Important scope: this is still an electrical/timing-window diagnostic.  Only
 * the first reset-vector word (00EAh at FFFF0h) is driven.  A row is reported
 * as CANDIDATE when the requested value becomes visible at the pad during the
 * hold interval and the passive observer still sees FFFF0->FFFF2->FFFF4.
 * It is not yet a full instruction-execution PASS.
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
    V3_HIZ = 0,
    V3_D1_NEXT_RISE,
    V3_D2_NEXT_FALL,
} v3_start_t;

typedef struct {
    const char *name;
    v3_start_t start;
    uint hold_falls;
    bool drive;
} v3_case_t;

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} v3_observer_t;

typedef struct {
    bool reset_ok;
    bool first_seen;
    bool start_ok;
    bool hold_ok;
    uint32_t first_t1_m33;
    uint16_t pre_pad;
    uint16_t out_latch;
    uint32_t oe;
    uint8_t clk_at_drive;
    uint8_t ale_at_drive;
    uint16_t pad0;
    uint16_t pad1;
    uint16_t pad2;
    uint16_t pad3;
    uint32_t observer_t1[4];
    uint observer_count;
} v3_result_t;

static const v3_case_t v3_cases[] = {
    {"HIZ",   V3_HIZ,          0u, false},
    {"D1-H1", V3_D1_NEXT_RISE, 1u, true},
    {"D1-H2", V3_D1_NEXT_RISE, 2u, true},
    {"D1-H3", V3_D1_NEXT_RISE, 3u, true},
    {"D2-H1", V3_D2_NEXT_FALL, 1u, true},
    {"D2-H2", V3_D2_NEXT_FALL, 2u, true},
    {"D2-H3", V3_D2_NEXT_FALL, 3u, true},
    {"HIZ-P", V3_HIZ,          0u, false},
};

static void v3_observer_init(v3_observer_t *o) {
    o->pio = pio1;
    o->sm = pio_claim_unused_sm(o->pio, true);
    o->offset = pio_add_program(o->pio, &perf_ale_observer_program);
    pio_sm_config c = perf_ale_observer_program_get_default_config(o->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(o->pio, o->sm, o->offset, &c);
}

static void v3_observer_start(v3_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
    pio_sm_clear_fifos(o->pio, o->sm);
    pio_sm_restart(o->pio, o->sm);
    pio_sm_exec(o->pio, o->sm, pio_encode_jmp(o->offset));
    pio_sm_set_enabled(o->pio, o->sm, true);
}

static void v3_observer_stop(v3_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
}

static bool __not_in_flash_func(v3_wait_fresh_rise)(void) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_CLK, false, time_us_64() + t, NULL)) return false;
    return wait_level_until(V30_PIN_CLK, true, time_us_64() + t, NULL);
}

static bool __not_in_flash_func(v3_wait_fresh_fall)(uint32_t *sample_out) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_CLK, true, time_us_64() + t, NULL)) return false;
    return wait_level_until(V30_PIN_CLK, false, time_us_64() + t, sample_out);
}

static bool __not_in_flash_func(v3_wait_first_ale_fall_hot)(uint32_t *last_high) {
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

static bool __not_in_flash_func(v3_advance_start)(v3_start_t start) {
    switch (start) {
        case V3_HIZ:
            return true;
        case V3_D1_NEXT_RISE:
            return v3_wait_fresh_rise();
        case V3_D2_NEXT_FALL:
            return v3_wait_fresh_fall(NULL);
    }
    return false;
}

static void __not_in_flash_func(v3_run_case)(perf_clock_t *clock,
                                              v3_observer_t *observer,
                                              const v3_case_t *tc,
                                              v3_result_t *r) {
    *r = (v3_result_t){0};
    release_ad();
    set_intr(false);
    hold_reset(true);
    v3_observer_start(observer);
    perf_clock_start(clock);

    r->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (!r->reset_ok) goto done;
    hold_reset(false);

    if (!v3_wait_first_ale_fall_hot(&r->first_t1_m33)) goto done;
    r->first_seen = true;
    r->pre_pad = decode_ad(sio_hw->gpio_in);

    r->start_ok = v3_advance_start(tc->start);
    if (!r->start_ok) goto done;

    if (tc->drive) {
        drive_data(0x00EAu, V30_BUS_LANES_WORD);
        const uint32_t s0 = sio_hw->gpio_in;
        r->out_latch = decode_ad(sio_hw->gpio_out);
        r->oe = sio_hw->gpio_oe & V30_AD_BUS_MASK;
        r->clk_at_drive = (uint8_t)sample_bit(s0, V30_PIN_CLK);
        r->ale_at_drive = (uint8_t)sample_bit(s0, V30_PIN_ALE);
        r->pad0 = decode_ad(s0);

        for (uint edge = 1u; edge <= tc->hold_falls; ++edge) {
            uint32_t se = 0u;
            if (!v3_wait_fresh_fall(&se)) {
                r->hold_ok = false;
                release_ad();
                goto done;
            }
            const uint16_t pv = decode_ad(se);
            if (edge == 1u) r->pad1 = pv;
            if (edge == 2u) r->pad2 = pv;
            if (edge == 3u) r->pad3 = pv;
        }
        r->hold_ok = true;
        release_ad();
    } else {
        const uint32_t s = sio_hw->gpio_in;
        r->out_latch = decode_ad(sio_hw->gpio_out);
        r->oe = sio_hw->gpio_oe & V30_AD_BUS_MASK;
        r->clk_at_drive = (uint8_t)sample_bit(s, V30_PIN_CLK);
        r->ale_at_drive = (uint8_t)sample_bit(s, V30_PIN_ALE);
        r->pad0 = decode_ad(s);
        r->hold_ok = true;
    }

    {
        const uint64_t deadline = time_us_64() + timeout_us_from_clocks(32u);
        while (pio_sm_get_rx_fifo_level(observer->pio, observer->sm) < 3u &&
               time_us_64() <= deadline) {
            tight_loop_contents();
        }
    }

done:
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);
    v3_observer_stop(observer);

    while (!pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) && r->observer_count < 4u)
        r->observer_t1[r->observer_count++] = pio_sm_get(observer->pio, observer->sm);
}

static bool v3_pad_seen_value(const v3_case_t *tc, const v3_result_t *r) {
    if (!tc->drive) return false;
    if (r->pad0 == 0x00EAu) return true;
    if (tc->hold_falls >= 1u && r->pad1 == 0x00EAu) return true;
    if (tc->hold_falls >= 2u && r->pad2 == 0x00EAu) return true;
    if (tc->hold_falls >= 3u && r->pad3 == 0x00EAu) return true;
    return false;
}

static bool v3_expected_prefetch(const v3_result_t *r) {
    if (r->observer_count < 3u) return false;
    return decode_address(r->observer_t1[0]) == 0xFFFF0u &&
           decode_address(r->observer_t1[1]) == 0xFFFF2u &&
           decode_address(r->observer_t1[2]) == 0xFFFF4u;
}

static const char *v3_result_name(const v3_case_t *tc, const v3_result_t *r) {
    if (!r->reset_ok || !r->first_seen || !r->start_ok || !r->hold_ok)
        return "FAIL";
    if (!tc->drive)
        return v3_expected_prefetch(r) ? "PASS" : "FAIL";
    if (r->out_latch != 0x00EAu || r->oe != V30_AD_BUS_MASK)
        return "FAIL";
    if (!v3_pad_seen_value(tc, r))
        return "FAIL";
    if (!v3_expected_prefetch(r))
        return "FAIL";
    return "CANDIDATE";
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 read timing matrix v3 - 0.300 MHz\n");
    printf("Start sweep      : D1 / D2 only\n");
    printf("Hold sweep       : 1 / 2 / 3 subsequent falling CLK edges\n");
    printf("Driven word      : 00EA at FFFF0h\n");
    printf("Observer         : passive PIO ALE latch\n");
    printf("Scoring          : CANDIDATE, not full execution PASS\n");
    printf("Canonical gate   : unchanged\n\n");
    printf("D1 = next fresh CLK rise after ALE fall\n");
    printf("D2 = next fresh CLK fall after ALE fall\n");
    printf("pad0 = immediate readback after OE; pad1..3 = readback at held falling edges\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);
    v3_observer_t observer;
    v3_observer_init(&observer);

    v3_result_t results[sizeof(v3_cases) / sizeof(v3_cases[0])];
    const uint32_t irq_state = save_and_disable_interrupts();
    for (uint i = 0u; i < sizeof(v3_cases) / sizeof(v3_cases[0]); ++i)
        v3_run_case(&clock, &observer, &v3_cases[i], &results[i]);
    restore_interrupts(irq_state);

    printf("case   first  pre  out   OE-mask   C A  pad0 pad1 pad2 pad3  PIO0   PIO1   PIO2   result\n");
    for (uint i = 0u; i < sizeof(v3_cases) / sizeof(v3_cases[0]); ++i) {
        const v3_case_t *tc = &v3_cases[i];
        const v3_result_t *r = &results[i];
        const uint32_t p0 = r->observer_count > 0u ? decode_address(r->observer_t1[0]) : 0u;
        const uint32_t p1 = r->observer_count > 1u ? decode_address(r->observer_t1[1]) : 0u;
        const uint32_t p2 = r->observer_count > 2u ? decode_address(r->observer_t1[2]) : 0u;
        printf("%-6s %05lX %04X %04X  %08lX  %u %u  %04X %04X %04X %04X  %05lX  %05lX  %05lX  %s\n",
               tc->name,
               (unsigned long)decode_address(r->first_t1_m33),
               (unsigned)r->pre_pad,
               (unsigned)r->out_latch,
               (unsigned long)r->oe,
               (unsigned)r->clk_at_drive,
               (unsigned)r->ale_at_drive,
               (unsigned)r->pad0,
               (unsigned)r->pad1,
               (unsigned)r->pad2,
               (unsigned)r->pad3,
               (unsigned long)p0,
               (unsigned long)p1,
               (unsigned long)p2,
               v3_result_name(tc, r));
    }

    printf("\nCANDIDATE requires:\n");
    printf("  output latch=00EA, AD OE enabled, 00EA visible at pad at least once,\n");
    printf("  and passive PIO observes FFFF0 -> FFFF2 -> FFFF4.\n");
    printf("A candidate still requires a subsequent full reset-vector service test.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
