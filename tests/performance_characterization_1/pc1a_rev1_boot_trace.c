/*
 * PC1-A Rev1 focused boot-trace diagnostic.
 *
 * Purpose: verify the remaining control-phase hypothesis without changing the
 * canonical Rev1 gate.  The Rev1 transparent-latch T1 rule already fixed the
 * cycle-zero address capture.  This target adds one extra phase anchor before
 * the control sample so the control sample is guaranteed to come from the
 * *next* complete CLK high->low phase after T1, matching the known-good
 * stepped engine rather than potentially reusing the T1 falling edge.
 */
#define main pc1a_rev1_300khz_original_main
#define capture_cycle pc1a_rev1_capture_cycle_original
#define run_point pc1a_rev1_run_point_original
#include "pc1a_rev1_300khz.c"
#undef run_point
#undef capture_cycle
#undef main

#define BOOT_TRACE_CYCLES 12u

typedef struct {
    uint32_t seq;
    uint32_t address;
    uint32_t t1_raw;
    uint32_t control_raw;
    uint16_t driven;
    uint16_t control_ad;
    uint16_t n;
    uint8_t type;
    uint8_t lanes;
} boot_trace_entry_t;

static bool __not_in_flash_func(capture_cycle_control_anchored)(
        v30_bus_cycle_t *cycle,
        uint16_t *ale_high_samples,
        rev1_fail_reason_t *reason) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    uint32_t sample = 0u;

    /* Every cycle starts from a known ALE-low state. */
    if (!wait_level_until(V30_PIN_ALE, false, time_us_64() + timeout_us, NULL)) {
        *reason = REV1_FAIL_ALE_LOW_TIMEOUT;
        return false;
    }
    if (!wait_level_until(V30_PIN_ALE, true, time_us_64() + timeout_us, &sample)) {
        *reason = REV1_FAIL_ALE_HIGH_TIMEOUT;
        return false;
    }

    /* Software-transparent latch: keep the final coherent ALE-high sample. */
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

    /*
     * Critical difference from the first Rev1 attempt:
     *
     * After ALE falls, first establish CLK LOW.  This guarantees the T1
     * falling edge has completed.  Only then wait for a fresh HIGH->LOW pulse
     * and sample control at that following falling edge.  The previous helper
     * could enter while CLK was still high and accidentally count the same T1
     * falling edge as the "next" falling edge.
     */
    if (!wait_level_until(V30_PIN_CLK, false, time_us_64() + timeout_us, NULL)) {
        *reason = REV1_FAIL_CONTROL_TIMEOUT;
        return false;
    }
    if (!wait_level_until(V30_PIN_CLK, true, time_us_64() + timeout_us, NULL)) {
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

    printf("\nPC1-A Rev1 focused boot trace - 0.300 MHz\n");
    printf("T1 capture      : last coherent ALE-high snapshot\n");
    printf("Control capture : explicit CLK-low anchor, then fresh HIGH->LOW edge\n");
    printf("Goal            : verify the first 12 physical bus cycles before changing the canonical gate\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    boot_trace_entry_t trace[BOOT_TRACE_CYCLES] = {0};
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

    for (uint seq = 0u; seq < BOOT_TRACE_CYCLES; ++seq) {
        v30_bus_cycle_t cycle;
        uint16_t n = 0u;
        rev1_fail_reason_t reason = REV1_FAIL_NONE;
        if (!capture_cycle_control_anchored(&cycle, &n, &reason)) {
            fail_reason = reason;
            break;
        }

        boot_trace_entry_t *e = &trace[captured++];
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
        if (!wait_falling_edges(2u)) {
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

    printf("Captured %u/%u boot cycles\n", captured, BOOT_TRACE_CYCLES);
    printf("Failure reason: %s\n\n", fail_reason_name(fail_reason));
    printf(" seq  address type lanes  n   driven CTRL_AD  T1_raw    CTRL_raw\n");
    for (uint i = 0u; i < captured; ++i) {
        const boot_trace_entry_t *e = &trace[i];
        printf("%4lu  %05lX   %u    %u   %3u  %04X   %04X   %08lX  %08lX\n",
               (unsigned long)e->seq,
               (unsigned long)e->address,
               (unsigned)e->type,
               (unsigned)e->lanes,
               (unsigned)e->n,
               (unsigned)e->driven,
               (unsigned)e->control_ad,
               (unsigned long)e->t1_raw,
               (unsigned long)e->control_raw);
    }

    printf("\nExpected known-good Gate-12 boot-address prefix:\n");
    printf("FFFF0 FFFF2 FFFF4 FFFF6 F0000 F0002 F0004 F0006 F0008 F000A F000C F000E\n");
    printf("This target is diagnostic only; it does not change PC1-A Rev1 PASS/FAIL status.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
