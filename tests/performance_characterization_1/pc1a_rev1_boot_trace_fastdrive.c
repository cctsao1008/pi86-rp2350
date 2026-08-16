/*
 * PC1-A Rev1 focused read-response latency diagnostic at 0.300 MHz.
 *
 * The release1 A/B test did not restore FFFF0 -> FFFF2. More importantly,
 * cycle zero requested drive=00EA but immediate pad readback was FFF6. That
 * means the current software path is not establishing the requested read data
 * on AD at the observed point in the free-running cycle.
 *
 * This target isolates response latency by removing the normal memory backend
 * from the first boot reads. It uses an SRAM-resident address->word lookup and
 * drives the response immediately after the corrected control sample. The
 * diagnostic asks one narrow question: does a minimal response path restore
 * the known-good reset-vector prefetch progression?
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#define FAST_TRACE_CYCLES 12u

typedef struct {
    uint32_t seq;
    uint32_t address;
    uint32_t t1_raw;
    uint32_t control_raw;
    uint16_t driven;
    uint16_t pad_rb;
    uint16_t n;
    uint8_t type;
    uint8_t lanes;
} fast_trace_entry_t;

static bool __not_in_flash_func(capture_cycle_control_anchored_fast)(
        v30_bus_cycle_t *cycle,
        uint16_t *ale_high_samples,
        rev1_fail_reason_t *reason) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    uint32_t sample = 0u;

    if (!wait_level_until(V30_PIN_ALE, false, time_us_64() + timeout_us, NULL)) {
        *reason = REV1_FAIL_ALE_LOW_TIMEOUT;
        return false;
    }
    if (!wait_level_until(V30_PIN_ALE, true, time_us_64() + timeout_us, &sample)) {
        *reason = REV1_FAIL_ALE_HIGH_TIMEOUT;
        return false;
    }

    uint32_t t1 = sample;
    uint16_t n = 0u;
    while (sample_bit(sample, V30_PIN_ALE) != 0u) {
        t1 = sample;
        if (n != UINT16_MAX) ++n;
        sample = sio_hw->gpio_in;
    }

    cycle->t1_sample = t1;
    cycle->address = decode_address(t1);
    cycle->a0 = (uint8_t)sample_bit(t1, V30_PIN_AD0);
    cycle->bhe_n = (uint8_t)sample_bit(t1, V30_PIN_BHE);
    cycle->lanes = decode_lanes(cycle->a0, cycle->bhe_n);

    /* End T1, then use the next fresh falling edge as the control anchor. */
    if (!wait_level_until(V30_PIN_CLK, false, time_us_64() + timeout_us, NULL) ||
        !wait_level_until(V30_PIN_CLK, true, time_us_64() + timeout_us, NULL)) {
        *reason = REV1_FAIL_CONTROL_TIMEOUT;
        return false;
    }

    uint32_t control = 0u;
    if (!wait_level_until(V30_PIN_CLK, false, time_us_64() + timeout_us, &control)) {
        *reason = REV1_FAIL_CONTROL_TIMEOUT;
        return false;
    }

    cycle->control_sample = control;
    cycle->iom = (uint8_t)sample_bit(control, V30_PIN_IOM);
    cycle->dtr = (uint8_t)sample_bit(control, V30_PIN_DTR);
    cycle->inta_n = (uint8_t)sample_bit(control, V30_PIN_INTA);
    cycle->type = decode_cycle_type(cycle->iom, cycle->dtr, cycle->inta_n);
    cycle->idle_steps = 0u;
    *ale_high_samples = n;

    if (cycle->lanes == V30_BUS_LANES_NONE &&
        cycle->type != V30_BUS_CYCLE_INTERRUPT_ACK) {
        *reason = REV1_FAIL_BUS_CYCLE;
        return false;
    }
    return true;
}

/*
 * Minimal SRAM-resident boot response lookup.
 * Values match init_test_image():
 *   FFFF0: EA 00
 *   FFFF2: 00 00
 *   FFFF4: F0 90
 * and the remaining reset-vector area is NOP-filled (90h).
 * The far jump enters F0000, where the Gate-12 test program starts.
 */
static bool __not_in_flash_func(fast_boot_read16)(uint32_t address,
                                                   uint16_t *value) {
    switch (address & 0xFFFFFu) {
        case 0xFFFF0u: *value = 0x00EAu; return true;
        case 0xFFFF2u: *value = 0x0000u; return true;
        case 0xFFFF4u: *value = 0x90F0u; return true;
        case 0xFFFF6u:
        case 0xFFFF8u:
        case 0xFFFFAu:
        case 0xFFFFCu:
        case 0xFFFFEu:
            *value = 0x9090u;
            return true;
        default:
            return false;
    }
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 fast-drive boot trace - 0.300 MHz\n");
    printf("T1 capture      : last coherent ALE-high snapshot\n");
    printf("Control capture : explicit CLK-low anchor, then fresh HIGH->LOW edge\n");
    printf("Read response   : SRAM-resident direct reset-vector lookup, immediate SIO drive\n");
    printf("Read release    : one subsequent falling edge\n");
    printf("Purpose         : isolate read-response latency from memory/backend overhead\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    fast_trace_entry_t trace[FAST_TRACE_CYCLES] = {0};
    uint captured = 0u;
    rev1_fail_reason_t fail_reason = REV1_FAIL_NONE;

    const uint32_t irq_state = save_and_disable_interrupts();

    release_ad();
    set_intr(false);
    hold_reset(true);
    perf_clock_start(&clock);

    if (!wait_reset_clocks(RESET_CLOCKS)) {
        fail_reason = REV1_FAIL_RESET_CLOCK_TIMEOUT;
        goto done;
    }
    hold_reset(false);

    for (uint seq = 0u; seq < FAST_TRACE_CYCLES; ++seq) {
        v30_bus_cycle_t cycle;
        uint16_t n = 0u;
        rev1_fail_reason_t reason = REV1_FAIL_NONE;
        if (!capture_cycle_control_anchored_fast(&cycle, &n, &reason)) {
            fail_reason = reason;
            break;
        }

        fast_trace_entry_t *e = &trace[captured++];
        e->seq = seq;
        e->address = cycle.address;
        e->t1_raw = cycle.t1_sample;
        e->control_raw = cycle.control_sample;
        e->n = n;
        e->type = (uint8_t)cycle.type;
        e->lanes = (uint8_t)cycle.lanes;

        if (cycle.type != V30_BUS_CYCLE_MEM_READ ||
            cycle.lanes != V30_BUS_LANES_WORD) {
            fail_reason = REV1_FAIL_BUS_CYCLE;
            break;
        }

        uint16_t driven = 0u;
        if (!fast_boot_read16(cycle.address, &driven)) {
            fail_reason = REV1_FAIL_MEMORY;
            break;
        }
        e->driven = driven;

        /* Minimal response path: no generic memory backend before the drive. */
        drive_data(driven, cycle.lanes);
        e->pad_rb = decode_ad(sio_hw->gpio_in);

        if (!wait_falling_edges(1u)) {
            release_ad();
            fail_reason = REV1_FAIL_CONTROL_TIMEOUT;
            break;
        }
        release_ad();
    }

done:
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(&clock);
    restore_interrupts(irq_state);

    printf("Captured %u/%u boot cycles\n", captured, FAST_TRACE_CYCLES);
    printf("Failure reason: %s\n\n", fail_reason_name(fail_reason));
    printf(" seq  address type lanes  n   driven pad_rb  T1_raw    CTRL_raw\n");
    for (uint i = 0u; i < captured; ++i) {
        const fast_trace_entry_t *e = &trace[i];
        printf("%4lu  %05lX   %u    %u   %3u  %04X   %04X   %08lX  %08lX\n",
               (unsigned long)e->seq,
               (unsigned long)e->address,
               (unsigned)e->type,
               (unsigned)e->lanes,
               (unsigned)e->n,
               (unsigned)e->driven,
               (unsigned)e->pad_rb,
               (unsigned long)e->t1_raw,
               (unsigned long)e->control_raw);
    }

    printf("\nPrimary discriminator:\n");
    printf("  desired start: FFFF0 -> FFFF2 -> FFFF4\n");
    printf("  release1 result: FFFF0 -> FFFF8 -> derailment, pad_rb != 00EA at cycle 0\n");
    printf("If fast-drive restores FFFF2 and pad_rb=00EA, CPU-side response latency is confirmed.\n");
    printf("If it does not, the control anchor / read-data window must be moved earlier.\n");
    printf("This target is diagnostic only; canonical pc1a_rev1_300khz is unchanged.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
