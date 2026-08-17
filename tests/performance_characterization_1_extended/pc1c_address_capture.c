/*
 * PC1-C0A passive address/control capture.
 *
 * This test deliberately provides no V30 read response. RESET qualification
 * runs with only the clock state machine enabled. After the clock is stopped
 * LOW, two passive PIO0 observers are armed, RESET is released, and the first
 * post-reset bus cycles are recorded with the default input synchronizers.
 */

#include <stdbool.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#include "v30/v30_pins.h"
#include "pc1b_first_cycle_phase_capture.pio.h"
#include "pc1c_bus_capture.pio.h"
#include "perf_continuous_clock.pio.h"

#define PC1C_CAPTURE_V30_HZ       300000u
#define RESET_CLOCKS                  20u
#define SIGNAL_TIMEOUT_CLOCKS         64u
#define CAPTURE_TIMEOUT_CLOCKS       320u
#define CAPTURE_TRACE_DEPTH            8u
#define FIRST_PHASE_COUNT              6u

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} capture_sm_t;

typedef struct {
    uint32_t address_raw;
    uint32_t control_raw;
} captured_cycle_t;

typedef struct {
    bool reset_ok;
    bool pre_release_clean;
    bool first_address_ok;
    bool first_memory_read;
    bool ad_passive;
    captured_cycle_t cycles[CAPTURE_TRACE_DEPTH];
    uint cycle_count;
    uint32_t phase_raw[FIRST_PHASE_COUNT];
    uint phase_count;
} capture_result_t;

static const uint8_t ad_pins[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(sample, ad_pins[bit]) << bit);
    return value;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static uint64_t timeout_us_from_clocks(uint32_t clocks) {
    return ((uint64_t)clocks * 1000000ull + PC1C_CAPTURE_V30_HZ - 1u) /
           PC1C_CAPTURE_V30_HZ + 2u;
}

static void prepare_header_high_z(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void init_control_outputs(void) {
    gpio_init(V30_PIN_RESET);
    gpio_disable_pulls(V30_PIN_RESET);
    gpio_put(V30_PIN_RESET, true);
    gpio_set_dir(V30_PIN_RESET, GPIO_OUT);

    gpio_init(V30_PIN_INTR);
    gpio_disable_pulls(V30_PIN_INTR);
    gpio_put(V30_PIN_INTR, false);
    gpio_set_dir(V30_PIN_INTR, GPIO_OUT);
}

static void release_ad(void) {
    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_pins[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static bool ad_is_passive(void) {
    if ((sio_hw->gpio_oe & V30_AD_BUS_MASK) != 0u) return false;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (gpio_get_function(ad_pins[bit]) != GPIO_FUNC_SIO) return false;
    }
    return true;
}

static void clock_init(capture_sm_t *clock) {
    clock->pio = pio0;
    clock->sm = pio_claim_unused_sm(clock->pio, true);
    clock->offset = pio_add_program(clock->pio, &perf_continuous_clk_program);
}

static void capture_clock_start(capture_sm_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);

    pio_sm_config c = perf_continuous_clk_program_get_default_config(clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)PC1C_CAPTURE_V30_HZ));
    pio_gpio_init(clock->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm, V30_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(clock->pio, clock->sm, clock->offset, &c) == PICO_OK);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
}

static void capture_clock_stop(capture_sm_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t edge_timeout = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    for (uint i = 0u; i < count; ++i) {
        uint64_t deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline && !gpio_get(V30_PIN_CLK))
            tight_loop_contents();
        if (!gpio_get(V30_PIN_CLK)) return false;

        deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline && gpio_get(V30_PIN_CLK))
            tight_loop_contents();
        if (gpio_get(V30_PIN_CLK)) return false;
    }
    return true;
}

static void bus_capture_init(capture_sm_t *capture) {
    capture->pio = pio0;
    capture->sm = pio_claim_unused_sm(capture->pio, true);
    capture->offset = pio_add_program(capture->pio, &pc1c_bus_capture_program);
    pio_sm_config c = pc1c_bus_capture_program_get_default_config(capture->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ALE);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(capture->pio, capture->sm, capture->offset, &c) == PICO_OK);
    pio_sm_set_enabled(capture->pio, capture->sm, false);
}

static void phase_capture_init(capture_sm_t *phase) {
    phase->pio = pio0;
    phase->sm = pio_claim_unused_sm(phase->pio, true);
    phase->offset = pio_add_program(phase->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(phase->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(phase->pio, phase->sm, phase->offset, &c) == PICO_OK);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
}

static void arm_capture(capture_sm_t *capture) {
    pio_sm_set_enabled(capture->pio, capture->sm, false);
    pio_sm_clear_fifos(capture->pio, capture->sm);
    pio_sm_restart(capture->pio, capture->sm);
    pio_sm_exec(capture->pio, capture->sm, pio_encode_jmp(capture->offset));
}

static void drain_cycle_pairs(capture_sm_t *capture, capture_result_t *result) {
    while (pio_sm_get_rx_fifo_level(capture->pio, capture->sm) >= 2u &&
           result->cycle_count < CAPTURE_TRACE_DEPTH) {
        captured_cycle_t *cycle = &result->cycles[result->cycle_count++];
        cycle->address_raw = pio_sm_get(capture->pio, capture->sm);
        cycle->control_raw = pio_sm_get(capture->pio, capture->sm);
    }
}

static void run_capture(capture_sm_t *clock,
                        capture_sm_t *capture,
                        capture_sm_t *phase,
                        capture_result_t *result) {
    *result = (capture_result_t){0};
    gpio_put(V30_PIN_INTR, false);
    gpio_put(V30_PIN_RESET, true);
    release_ad();

    capture_clock_start(clock);
    result->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    capture_clock_stop(clock);

    arm_capture(capture);
    arm_capture(phase);
    result->pre_release_clean =
        pio_sm_is_rx_fifo_empty(capture->pio, capture->sm) &&
        pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
        ad_is_passive();

    pio_enable_sm_mask_in_sync(pio0,
        (1u << capture->sm) | (1u << phase->sm));

    if (result->reset_ok && result->pre_release_clean) {
        gpio_put(V30_PIN_RESET, false);
        capture_clock_start(clock);
        const uint64_t deadline =
            time_us_64() + timeout_us_from_clocks(CAPTURE_TIMEOUT_CLOCKS);
        while (time_us_64() <= deadline &&
               result->cycle_count < CAPTURE_TRACE_DEPTH) {
            drain_cycle_pairs(capture, result);
            tight_loop_contents();
        }
    }

    gpio_put(V30_PIN_RESET, true);
    capture_clock_stop(clock);
    pio_sm_set_enabled(capture->pio, capture->sm, false);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
    drain_cycle_pairs(capture, result);
    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           result->phase_count < FIRST_PHASE_COUNT) {
        result->phase_raw[result->phase_count++] =
            pio_sm_get(phase->pio, phase->sm);
    }

    release_ad();
    gpio_put(V30_PIN_INTR, false);
    result->ad_passive = ad_is_passive();
    result->first_address_ok = result->cycle_count > 0u &&
        decode_address(result->cycles[0].address_raw) == 0xFFFF0u;
    if (result->cycle_count > 0u) {
        const uint32_t control = result->cycles[0].control_raw;
        result->first_memory_read =
            sample_bit(control, V30_PIN_IOM) != 0u &&
            sample_bit(control, V30_PIN_BUFRW) == 0u &&
            sample_bit(control, V30_PIN_INTA) != 0u;
    }
}

static bool result_valid(const capture_result_t *result) {
    return result->reset_ok && result->pre_release_clean &&
           result->cycle_count > 0u && result->phase_count == FIRST_PHASE_COUNT;
}

static bool result_pass(const capture_result_t *result) {
    return result_valid(result) && result->first_address_ok &&
           result->first_memory_read && result->ad_passive;
}

static void print_result(const capture_result_t *result) {
    printf("\nPC1-C0A Address Capture Test - 0.300 MHz\n");
    printf("RESET qualification : clock-only; capture SMs disabled\n");
    printf("Measurement epoch   : arm after RESET clocks with CLK stopped LOW\n");
    printf("Bus ownership       : passive only; PIO1 and DMA unused\n");
    printf("Input synchronizers : SDK defaults\n\n");
    printf("RESET clock count       = %s\n", result->reset_ok ? "PASS" : "FAIL");
    printf("PRE-RESET EVENT LEAK    = %s\n",
           result->pre_release_clean ? "NO" : "YES / INVALID");
    printf("FIRST post-reset address= %s",
           result->first_address_ok ? "FFFF0 PASS" : "FAIL");
    if (result->cycle_count > 0u)
        printf(" (observed %05lX)",
               (unsigned long)decode_address(result->cycles[0].address_raw));
    printf("\n");
    printf("FIRST cycle type        = %s\n",
           result->first_memory_read ? "MEMORY READ PASS" : "FAIL");
    printf("AD bus ownership        = %s\n",
           result->ad_passive ? "PASSIVE PASS" : "FAIL");

    printf("\n[ADDRESS / CONTROL TRACE]\n");
    printf("idx address raw_addr raw_ctrl ALE IOM BUFRW INTA BHE A0\n");
    for (uint i = 0u; i < result->cycle_count; ++i) {
        const captured_cycle_t *cycle = &result->cycles[i];
        printf("%02u  %05lX  %08lX %08lX  %u   %u   %u    %u   %u   %u\n",
               i,
               (unsigned long)decode_address(cycle->address_raw),
               (unsigned long)cycle->address_raw,
               (unsigned long)cycle->control_raw,
               (unsigned)sample_bit(cycle->control_raw, V30_PIN_ALE),
               (unsigned)sample_bit(cycle->control_raw, V30_PIN_IOM),
               (unsigned)sample_bit(cycle->control_raw, V30_PIN_BUFRW),
               (unsigned)sample_bit(cycle->control_raw, V30_PIN_INTA),
               (unsigned)sample_bit(cycle->control_raw, V30_PIN_BHE),
               (unsigned)sample_bit(cycle->address_raw, V30_PIN_AD0));
    }

    static const char *const phase_names[FIRST_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    printf("\n[FIRST-CYCLE GPIO SNAPSHOTS]\n");
    printf("phase raw_gpio  ALE CLK IOM BUFRW INTA BHE AD16\n");
    for (uint i = 0u; i < result->phase_count; ++i) {
        const uint32_t raw = result->phase_raw[i];
        printf("%-4s  %08lX   %u   %u   %u   %u    %u   %u  %04X\n",
               phase_names[i], (unsigned long)raw,
               (unsigned)sample_bit(raw, V30_PIN_ALE),
               (unsigned)sample_bit(raw, V30_PIN_CLK),
               (unsigned)sample_bit(raw, V30_PIN_IOM),
               (unsigned)sample_bit(raw, V30_PIN_BUFRW),
               (unsigned)sample_bit(raw, V30_PIN_INTA),
               (unsigned)sample_bit(raw, V30_PIN_BHE),
               (unsigned)decode_ad(raw));
    }

    printf("\nMEASUREMENT EPOCH          = %s\n",
           result_valid(result) ? "VALID" : "INVALID");
    printf("PC1-C ADDRESS CAPTURE RESULT = %s\n",
           result_pass(result) ? "PASS" :
           (result_valid(result) ? "FAIL" : "INVALID"));
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    prepare_header_high_z();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    capture_sm_t clock;
    capture_sm_t capture;
    capture_sm_t phase;
    clock_init(&clock);
    bus_capture_init(&capture);
    phase_capture_init(&phase);

    capture_result_t result;
    run_capture(&clock, &capture, &phase, &result);
    print_result(&result);
    fflush(stdout);
    while (true) tight_loop_contents();
}
