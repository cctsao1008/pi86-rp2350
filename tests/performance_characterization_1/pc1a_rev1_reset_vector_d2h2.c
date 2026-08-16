/*
 * PC1-A Rev1 reset-vector service diagnostic at 0.300 MHz.
 *
 * Purpose:
 *   Validate the D2-H2 read-response timing candidate discovered by the v3
 *   timing matrix using the complete reset-vector far-jump instruction.
 *
 * The first three reset-vector word reads are serviced with the known ROM
 * contents:
 *   FFFF0h -> 00EAh  (EA 00)
 *   FFFF2h -> 0000h  (00 00)
 *   FFFF4h -> 90F0h  (F0 90)
 *
 * For every serviced word, D2-H2 means:
 *   1. capture the final ALE-high T1 snapshot;
 *   2. after ALE falls, wait for the next fresh CLK falling edge (D2);
 *   3. assert the prepared word on AD15:0;
 *   4. hold through two subsequent fresh CLK falling edges (H2);
 *   5. release AD to high-Z.
 *
 * A passive PIO observer independently records ALE-high addresses.  PASS
 * requires the observed sequence FFFF0 -> FFFF2 -> FFFF4 -> F0000, proving
 * that the physical V30 accepted the complete far-jump encoding and changed
 * control flow to the ROM base.  This diagnostic does not modify the canonical
 * PC1-A Rev1 gate.
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
} rv_observer_t;

typedef struct {
    uint32_t t1;
    uint32_t address;
    uint16_t value;
    uint16_t pre_pad;
    uint16_t out_latch;
    uint16_t pad0;
    uint16_t pad1;
    uint16_t pad2;
    uint32_t oe;
    bool capture_ok;
    bool d2_ok;
    bool h2_ok;
} rv_service_t;

typedef struct {
    bool reset_ok;
    rv_service_t service[3];
    uint32_t observer_t1[4];
    uint observer_count;
    bool pass;
} rv_result_t;

static const uint32_t expected_address[3] = {
    0xFFFF0u,
    0xFFFF2u,
    0xFFFF4u,
};

static const uint16_t reset_words[3] = {
    0x00EAu,
    0x0000u,
    0x90F0u,
};

static void rv_observer_init(rv_observer_t *o) {
    o->pio = pio1;
    o->sm = pio_claim_unused_sm(o->pio, true);
    o->offset = pio_add_program(o->pio, &perf_ale_observer_program);

    pio_sm_config c = perf_ale_observer_program_get_default_config(o->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    pio_sm_init(o->pio, o->sm, o->offset, &c);
}

static void rv_observer_start(rv_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
    pio_sm_clear_fifos(o->pio, o->sm);
    pio_sm_restart(o->pio, o->sm);
    pio_sm_exec(o->pio, o->sm, pio_encode_jmp(o->offset));
    pio_sm_set_enabled(o->pio, o->sm, true);
}

static void rv_observer_stop(rv_observer_t *o) {
    pio_sm_set_enabled(o->pio, o->sm, false);
}

static bool __not_in_flash_func(rv_wait_fresh_fall)(uint32_t *sample_out) {
    const uint64_t t = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_CLK, true, time_us_64() + t, NULL)) return false;
    return wait_level_until(V30_PIN_CLK, false, time_us_64() + t, sample_out);
}

static bool __not_in_flash_func(rv_wait_ale_fall_hot)(uint32_t *last_high) {
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

static bool __not_in_flash_func(rv_service_word_d2h2)(uint16_t value,
                                                       rv_service_t *s) {
    s->value = value;

    if (!rv_wait_ale_fall_hot(&s->t1)) return false;
    s->capture_ok = true;
    s->address = decode_address(s->t1);
    s->pre_pad = decode_ad(sio_hw->gpio_in);

    /* D2: wait for the next fresh falling edge after ALE fall. */
    if (!rv_wait_fresh_fall(NULL)) return false;
    s->d2_ok = true;

    drive_data(value, V30_BUS_LANES_WORD);
    const uint32_t p0 = sio_hw->gpio_in;
    s->out_latch = decode_ad(sio_hw->gpio_out);
    s->oe = sio_hw->gpio_oe & V30_AD_BUS_MASK;
    s->pad0 = decode_ad(p0);

    /* H2: retain the data through two subsequent fresh falling edges. */
    uint32_t p1 = 0u;
    uint32_t p2 = 0u;
    if (!rv_wait_fresh_fall(&p1)) {
        release_ad();
        return false;
    }
    s->pad1 = decode_ad(p1);

    if (!rv_wait_fresh_fall(&p2)) {
        release_ad();
        return false;
    }
    s->pad2 = decode_ad(p2);
    s->h2_ok = true;

    release_ad();
    return true;
}

static bool rv_service_electrically_ok(const rv_service_t *s) {
    return s->capture_ok && s->d2_ok && s->h2_ok &&
           s->out_latch == s->value &&
           s->oe == V30_AD_BUS_MASK &&
           (s->pad0 == s->value || s->pad1 == s->value || s->pad2 == s->value);
}

static void __not_in_flash_func(rv_run)(perf_clock_t *clock,
                                         rv_observer_t *observer,
                                         rv_result_t *r) {
    *r = (rv_result_t){0};

    release_ad();
    set_intr(false);
    hold_reset(true);
    rv_observer_start(observer);
    perf_clock_start(clock);

    r->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    if (!r->reset_ok) goto done;

    hold_reset(false);

    for (uint i = 0u; i < 3u; ++i) {
        if (!rv_service_word_d2h2(reset_words[i], &r->service[i])) goto done;
        if (r->service[i].address != expected_address[i]) goto done;
    }

    /*
     * Do not service F0000.  The fourth passive ALE observation is the
     * discriminator: if the complete far jump was accepted, it must be F0000.
     */
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
    rv_observer_stop(observer);

    while (!pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
           r->observer_count < 4u) {
        r->observer_t1[r->observer_count++] = pio_sm_get(observer->pio, observer->sm);
    }

    bool service_ok = true;
    for (uint i = 0u; i < 3u; ++i) {
        service_ok = service_ok &&
                     r->service[i].address == expected_address[i] &&
                     rv_service_electrically_ok(&r->service[i]);
    }

    r->pass = r->reset_ok && service_ok && r->observer_count >= 4u &&
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

    printf("\nPC1-A Rev1 D2-H2 full reset-vector service - 0.300 MHz\n");
    printf("Read timing       : D2 start / H2 hold\n");
    printf("Service scope     : FFFF0, FFFF2, FFFF4 only\n");
    printf("Reset-vector code : EA 00 00 00 F0 90\n");
    printf("Observer          : passive PIO ALE latch\n");
    printf("PASS discriminator: FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("Canonical gate    : unchanged\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);
    rv_observer_t observer;
    rv_observer_init(&observer);
    rv_result_t result;

    const uint32_t irq_state = save_and_disable_interrupts();
    rv_run(&clock, &observer, &result);
    restore_interrupts(irq_state);

    printf("RESET clock count = %s\n\n", result.reset_ok ? "PASS" : "FAIL");
    printf("idx address value  out   OE-mask   pad0 pad1 pad2  electrical\n");
    for (uint i = 0u; i < 3u; ++i) {
        const rv_service_t *s = &result.service[i];
        printf(" %u  %05lX  %04X  %04X  %08lX  %04X %04X %04X  %s\n",
               i,
               (unsigned long)s->address,
               (unsigned)s->value,
               (unsigned)s->out_latch,
               (unsigned long)s->oe,
               (unsigned)s->pad0,
               (unsigned)s->pad1,
               (unsigned)s->pad2,
               rv_service_electrically_ok(s) ? "PASS" : "FAIL");
    }

    printf("\nPIO ALE sequence (%u captured):\n", result.observer_count);
    for (uint i = 0u; i < result.observer_count; ++i) {
        printf("  PIO%u = %05lX\n",
               i,
               (unsigned long)decode_address(result.observer_t1[i]));
    }

    printf("\nExpected control-flow sequence = FFFF0 -> FFFF2 -> FFFF4 -> F0000\n");
    printf("RESET VECTOR D2-H2 RESULT      = %s\n", result.pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
