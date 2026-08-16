/*
 * PC1-A Rev1 focused AD-release diagnostic at 0.300 MHz.
 *
 * This is an A/B follow-up to pc1a_rev1_boot_trace.c.  The previous trace
 * proved cycle-zero address/control capture but then skipped from FFFF0 to
 * FFFF8.  The likely remaining difference from the host-stepped engine is
 * that a free-running clock does not pause after T4: holding AD through two
 * additional falling edges can overlap the next T1 before software releases
 * the bus.
 *
 * This target keeps the corrected T1 and control anchors, but releases AD
 * after ONE fresh falling edge following data drive.  It also records an
 * immediate SIO pad readback of the driven word.  The canonical Rev1 gate is
 * not changed by this diagnostic.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#define RELEASE1_TRACE_CYCLES 12u

typedef struct {
    uint32_t seq;
    uint32_t address;
    uint32_t t1_raw;
    uint32_t control_raw;
    uint16_t driven;
    uint16_t drive_readback;
    uint16_t control_ad;
    uint16_t n;
    uint8_t type;
    uint8_t lanes;
} release1_trace_entry_t;

static bool __not_in_flash_func(capture_cycle_control_anchored_release1)(
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

    /* End T1 explicitly, then capture control on the next fresh falling edge. */
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

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 AD-release A/B boot trace - 0.300 MHz\n");
    printf("T1 capture      : last coherent ALE-high snapshot\n");
    printf("Control capture : explicit CLK-low anchor, then fresh HIGH->LOW edge\n");
    printf("Read release    : ONE falling edge after data drive\n");
    printf("Purpose         : test whether two-edge hold overlaps the next T1 on a free-running clock\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    release1_trace_entry_t trace[RELEASE1_TRACE_CYCLES] = {0};
    uint captured = 0u;
    rev1_fail_reason_t fail_reason = REV1_FAIL_NONE;

    init_test_image();
    pi86_memory_t memory;
    pi86_memory_init(&memory, ram, RAM_BASE, RAM_SIZE, rom, PI86_ROM_BASE, ROM_SIZE);

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

    for (uint seq = 0u; seq < RELEASE1_TRACE_CYCLES; ++seq) {
        v30_bus_cycle_t cycle;
        uint16_t n = 0u;
        rev1_fail_reason_t reason = REV1_FAIL_NONE;
        if (!capture_cycle_control_anchored_release1(&cycle, &n, &reason)) {
            fail_reason = reason;
            break;
        }

        release1_trace_entry_t *e = &trace[captured++];
        e->seq = seq;
        e->address = cycle.address;
        e->t1_raw = cycle.t1_sample;
        e->control_raw = cycle.control_sample;
        e->control_ad = decode_ad(cycle.control_sample);
        e->n = n;
        e->type = (uint8_t)cycle.type;
        e->lanes = (uint8_t)cycle.lanes;

        if (seq == 0u && !(cycle.address == RESET_VECTOR_ADDR &&
                           cycle.type == V30_BUS_CYCLE_MEM_READ &&
                           cycle.lanes == V30_BUS_LANES_WORD)) {
            fail_reason = REV1_FAIL_CYCLE0_ASSERT;
            break;
        }

        if (cycle.type != V30_BUS_CYCLE_MEM_READ) {
            fail_reason = REV1_FAIL_BUS_CYCLE;
            break;
        }

        uint16_t driven = 0u;
        if (!memory_read(&memory, &cycle, &driven)) {
            fail_reason = REV1_FAIL_MEMORY;
            break;
        }
        e->driven = driven;

        drive_data(driven, cycle.lanes);
        e->drive_readback = decode_ad(sio_hw->gpio_in);

        /*
         * A/B variable: release immediately after one subsequent falling edge.
         * If this restores FFFF0 -> FFFF2 -> FFFF4, the previous two-edge hold
         * was crossing the free-running turnaround into the next T1.
         */
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

    printf("Captured %u/%u boot cycles\n", captured, RELEASE1_TRACE_CYCLES);
    printf("Failure reason: %s\n\n", fail_reason_name(fail_reason));
    printf(" seq  address type lanes  n   driven pad_rb CTRL_AD  T1_raw    CTRL_raw\n");
    for (uint i = 0u; i < captured; ++i) {
        const release1_trace_entry_t *e = &trace[i];
        printf("%4lu  %05lX   %u    %u   %3u  %04X   %04X   %04X   %08lX  %08lX\n",
               (unsigned long)e->seq,
               (unsigned long)e->address,
               (unsigned)e->type,
               (unsigned)e->lanes,
               (unsigned)e->n,
               (unsigned)e->driven,
               (unsigned)e->drive_readback,
               (unsigned)e->control_ad,
               (unsigned long)e->t1_raw,
               (unsigned long)e->control_raw);
    }

    printf("\nPrimary discriminator:\n");
    printf("  expected first reads: FFFF0 -> FFFF2 -> FFFF4\n");
    printf("  previous two-edge trace: FFFF0 -> FFFF8 -> derailment\n");
    printf("If the first three reads are restored, AD release timing is the next confirmed defect.\n");
    printf("This target is diagnostic only; canonical pc1a_rev1_300khz is unchanged.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
