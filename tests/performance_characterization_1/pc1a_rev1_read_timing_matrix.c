/*
 * PC1-A Rev1 read-response timing matrix at 0.300 MHz.
 *
 * This diagnostic services only the first reset-vector read (FFFF0h) and then
 * observes the next ALE address.  It intentionally avoids the generic memory,
 * PIC, PIT, and full execution paths so that the only swept variable is when
 * RP2350 asserts the AD output relative to the end of the first ALE window.
 *
 * Functional rows drive the correct reset-vector word 00EAh.  A no-drive row
 * is the control.  One A55Ah row is included only as an electrical ownership
 * probe; its next address is not scored because the wrong instruction word can
 * legitimately change CPU execution.
 *
 * The hold/release rule for driven rows is identical: after asserting the word,
 * keep AD driven until one subsequent falling CLK edge, then release to high-Z.
 * This is a coarse software-phase diagnostic, not a nanosecond timing claim.
 * Canonical PC1-A Rev1 remains unchanged.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

typedef enum {
    MATRIX_HIZ = 0,
    MATRIX_D0_ALE_FALL_IMMEDIATE,
    MATRIX_D1_FIRST_LOW,
    MATRIX_D2_NEXT_HIGH,
    MATRIX_D3_NEXT_LOW,
    MATRIX_D4_SECOND_HIGH,
} matrix_phase_t;

typedef struct {
    const char *name;
    matrix_phase_t phase;
    uint16_t value;
    bool drive;
    bool score_functional;
} matrix_case_t;

typedef struct {
    bool reset_ok;
    bool first_ok;
    bool phase_ok;
    bool hold_ok;
    bool next_ok;
    uint32_t first_address;
    uint32_t next_address;
    uint16_t first_n;
    uint16_t next_n;
    uint16_t pre_pad;
    uint16_t pad_after_drive;
    uint32_t oe_after_drive;
    uint8_t clk_at_drive;
    uint8_t ale_at_drive;
} matrix_result_t;

static const matrix_case_t cases[] = {
    {"HIZ",       MATRIX_HIZ,                   0x0000u, false, true},
    {"D0",        MATRIX_D0_ALE_FALL_IMMEDIATE, 0x00EAu, true,  true},
    {"D1",        MATRIX_D1_FIRST_LOW,          0x00EAu, true,  true},
    {"D2",        MATRIX_D2_NEXT_HIGH,          0x00EAu, true,  true},
    {"D3",        MATRIX_D3_NEXT_LOW,           0x00EAu, true,  true},
    {"D4",        MATRIX_D4_SECOND_HIGH,        0x00EAu, true,  true},
    {"D0-A55A",   MATRIX_D0_ALE_FALL_IMMEDIATE, 0xA55Au, true,  false},
    {"HIZ-POST",  MATRIX_HIZ,                   0x0000u, false, true},
};

static bool __not_in_flash_func(capture_ale_window)(uint32_t *t1,
                                                     uint16_t *n,
                                                     uint32_t *after_fall) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    uint32_t sample = 0u;

    if (!wait_level_until(V30_PIN_ALE, false, time_us_64() + timeout_us, NULL))
        return false;
    if (!wait_level_until(V30_PIN_ALE, true, time_us_64() + timeout_us, &sample))
        return false;

    uint32_t last = sample;
    uint16_t count = 0u;
    while (sample_bit(sample, V30_PIN_ALE) != 0u) {
        last = sample;
        if (count != UINT16_MAX) ++count;
        sample = sio_hw->gpio_in;
    }

    *t1 = last;
    *n = count;
    *after_fall = sample;
    return true;
}

static bool __not_in_flash_func(advance_to_phase)(matrix_phase_t phase) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);

    switch (phase) {
        case MATRIX_HIZ:
        case MATRIX_D0_ALE_FALL_IMMEDIATE:
            return true;

        case MATRIX_D1_FIRST_LOW:
            return wait_level_until(V30_PIN_CLK, false,
                                    time_us_64() + timeout_us, NULL);

        case MATRIX_D2_NEXT_HIGH:
            if (!wait_level_until(V30_PIN_CLK, false,
                                  time_us_64() + timeout_us, NULL)) return false;
            return wait_level_until(V30_PIN_CLK, true,
                                    time_us_64() + timeout_us, NULL);

        case MATRIX_D3_NEXT_LOW:
            if (!wait_level_until(V30_PIN_CLK, false,
                                  time_us_64() + timeout_us, NULL)) return false;
            if (!wait_level_until(V30_PIN_CLK, true,
                                  time_us_64() + timeout_us, NULL)) return false;
            return wait_level_until(V30_PIN_CLK, false,
                                    time_us_64() + timeout_us, NULL);

        case MATRIX_D4_SECOND_HIGH:
            if (!wait_level_until(V30_PIN_CLK, false,
                                  time_us_64() + timeout_us, NULL)) return false;
            if (!wait_level_until(V30_PIN_CLK, true,
                                  time_us_64() + timeout_us, NULL)) return false;
            if (!wait_level_until(V30_PIN_CLK, false,
                                  time_us_64() + timeout_us, NULL)) return false;
            return wait_level_until(V30_PIN_CLK, true,
                                    time_us_64() + timeout_us, NULL);
    }
    return false;
}

static void __not_in_flash_func(run_case)(perf_clock_t *clock,
                                          const matrix_case_t *tc,
                                          matrix_result_t *r) {
    *r = (matrix_result_t){0};

    release_ad();
    set_intr(false);
    hold_reset(true);
    perf_clock_start(clock);

    r->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (!r->reset_ok) goto done;

    hold_reset(false);

    uint32_t first_t1 = 0u;
    uint32_t after_fall = 0u;
    uint16_t first_n = 0u;
    if (!capture_ale_window(&first_t1, &first_n, &after_fall)) goto done;

    r->first_address = decode_address(first_t1);
    r->first_n = first_n;
    r->first_ok = r->first_address == RESET_VECTOR_ADDR &&
                  decode_lanes((uint8_t)sample_bit(first_t1, V30_PIN_AD0),
                               (uint8_t)sample_bit(first_t1, V30_PIN_BHE)) == V30_BUS_LANES_WORD &&
                  sample_bit(after_fall, V30_PIN_IOM) != 0u &&
                  sample_bit(after_fall, V30_PIN_DTR) == 0u &&
                  sample_bit(after_fall, V30_PIN_INTA) != 0u;
    if (!r->first_ok) goto done;

    r->pre_pad = decode_ad(sio_hw->gpio_in);
    r->phase_ok = advance_to_phase(tc->phase);
    if (!r->phase_ok) goto done;

    if (tc->drive) {
        drive_data(tc->value, V30_BUS_LANES_WORD);
        const uint32_t driven_sample = sio_hw->gpio_in;
        r->pad_after_drive = decode_ad(driven_sample);
        r->oe_after_drive = sio_hw->gpio_oe & V30_AD_BUS_MASK;
        r->clk_at_drive = (uint8_t)sample_bit(driven_sample, V30_PIN_CLK);
        r->ale_at_drive = (uint8_t)sample_bit(driven_sample, V30_PIN_ALE);

        r->hold_ok = wait_falling_edges(1u);
        release_ad();
        if (!r->hold_ok) goto done;
    } else {
        const uint32_t s = sio_hw->gpio_in;
        r->pad_after_drive = decode_ad(s);
        r->oe_after_drive = sio_hw->gpio_oe & V30_AD_BUS_MASK;
        r->clk_at_drive = (uint8_t)sample_bit(s, V30_PIN_CLK);
        r->ale_at_drive = (uint8_t)sample_bit(s, V30_PIN_ALE);
        r->hold_ok = true;
    }

    {
        uint32_t next_t1 = 0u;
        uint32_t next_after_fall = 0u;
        uint16_t next_n = 0u;
        if (capture_ale_window(&next_t1, &next_n, &next_after_fall)) {
            (void)next_after_fall;
            r->next_ok = true;
            r->next_address = decode_address(next_t1);
            r->next_n = next_n;
        }
    }

done:
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);
}

static const char *functional_result(const matrix_case_t *tc,
                                     const matrix_result_t *r) {
    if (!tc->score_functional) return "ELEC";
    if (!r->reset_ok || !r->first_ok || !r->phase_ok || !r->hold_ok || !r->next_ok)
        return "FAIL";
    if (!tc->drive)
        return r->next_address == 0xFFFF2u ? "PASS" : "FAIL";
    if (r->pad_after_drive != tc->value) return "FAIL";
    return r->next_address == 0xFFFF2u ? "PASS" : "FAIL";
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 read-response timing matrix - 0.300 MHz\n");
    printf("Scope            : first reset-vector read only\n");
    printf("Functional data  : 00EA for FFFF0h\n");
    printf("Release rule     : one subsequent falling edge after drive\n");
    printf("Control rows     : HIZ before/after matrix\n");
    printf("Electrical probe : D0 with A55A (not functionally scored)\n");
    printf("Canonical gate   : unchanged\n\n");
    printf("Phase definitions after ALE falls:\n");
    printf("  D0 = immediate\n");
    printf("  D1 = first CLK low\n");
    printf("  D2 = next CLK high\n");
    printf("  D3 = next CLK low\n");
    printf("  D4 = following CLK high (late negative-control candidate)\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    matrix_result_t results[sizeof(cases) / sizeof(cases[0])];
    const uint32_t irq_state = save_and_disable_interrupts();
    for (uint i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i)
        run_case(&clock, &cases[i], &results[i]);
    restore_interrupts(irq_state);

    printf("case      first   n  pre   value  pad   OE-mask   C A  next    n  result\n");
    for (uint i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const matrix_case_t *tc = &cases[i];
        const matrix_result_t *r = &results[i];
        printf("%-9s %05lX %3u %04X  %04X  %04X  %08lX  %u %u  %05lX %3u  %s\n",
               tc->name,
               (unsigned long)r->first_address,
               (unsigned)r->first_n,
               (unsigned)r->pre_pad,
               (unsigned)tc->value,
               (unsigned)r->pad_after_drive,
               (unsigned long)r->oe_after_drive,
               (unsigned)r->clk_at_drive,
               (unsigned)r->ale_at_drive,
               (unsigned long)r->next_address,
               (unsigned)r->next_n,
               functional_result(tc, r));
    }

    printf("\nFunctional PASS requires:\n");
    printf("  first cycle FFFF0 / MEM_READ / WORD, requested 00EA visible at pad,\n");
    printf("  and next ALE address FFFF2. HIZ rows require only FFFF0 -> FFFF2.\n");
    printf("A55A is electrical-only because deliberately wrong instruction data can change execution.\n");
    printf("This matrix is a coarse software-phase experiment; do not interpret it as ns-level timing.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
