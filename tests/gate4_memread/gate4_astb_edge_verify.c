#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate4_step_clock.pio.h"
#include "gate4_astb_edge_verify.pio.h"

#define STEP_PIO_CLOCK_HZ 2000000u
#define RESET_CLOCKS 20u
#define VERIFY_CLOCKS 20u
#define COUNTER_SEED 0xffffffffu

static void configure_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void drive_cpu_input(uint gpio, bool level) {
    gpio_init(gpio);
    gpio_disable_pulls(gpio);
    gpio_put(gpio, level);
    gpio_set_dir(gpio, GPIO_OUT);
}

static void release_ad_bus(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static void init_step_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_step_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (float)STEP_PIO_CLOCK_HZ);

    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void clock_step(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 1u);
    (void)pio_sm_get_blocking(pio, sm);
}

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void init_edge_counter(PIO pio,
                              uint sm,
                              uint offset,
                              uint signal_gpio,
                              bool rising) {
    pio_sm_config c = rising
        ? gate4_edge_rise_counter_program_get_default_config(offset)
        : gate4_edge_fall_counter_program_get_default_config(offset);

    sm_config_set_in_pins(&c, signal_gpio);
    sm_config_set_clkdiv(&c, 1.0f);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);

    pio_sm_put_blocking(pio, sm, COUNTER_SEED);
    pio_sm_exec(pio, sm, pio_encode_pull(false, false));
    pio_sm_exec(pio, sm, pio_encode_mov(pio_x, pio_osr));
}

static uint32_t read_counter_x(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_exec(pio, sm, pio_encode_mov(pio_isr, pio_x));
    pio_sm_exec(pio, sm, pio_encode_push(false, false));
    return pio_sm_get_blocking(pio, sm);
}

static uint32_t edge_count_from_x(uint32_t x) {
    return COUNTER_SEED - x;
}

int main(void) {
    configure_header_high_z();
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();

    /*
     * Keep the V30 safely halted until the host has opened the USB CDC
     * interface.  This makes the serial connection the explicit test trigger:
     * no RESET release and no test clocks occur before CDC is connected.
     */
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    sleep_ms(100);

    printf("\npi86-rp2350 Gate 4 minimal ASTB edge verifier\n");
    printf("USB CDC connected: starting hardware test now.\n");
    printf("Independent method: four PIO state machines count ASTB/CLK rise/fall edges directly.\n");
    printf("No DMA, no AD decode, no address decode, no ALE trigger, no PSRAM.\n");
    printf("AD bus remains high-Z for the entire test.\n\n");
    fflush(stdout);

    PIO step_pio = pio0;
    const uint step_sm = pio_claim_unused_sm(step_pio, true);
    const uint step_offset = pio_add_program(step_pio, &gate4_step_clk_program);
    init_step_clock(step_pio, step_sm, step_offset);

    for (uint i = 0; i < RESET_CLOCKS; ++i) {
        clock_step(step_pio, step_sm);
    }

    const uint32_t astb_before_release = gpio_get(V30_PIN_ALE);
    const uint32_t clk_before_release = gpio_get(V30_PIN_CLK);

    PIO count_pio = pio1;
    const uint rise_offset = pio_add_program(count_pio, &gate4_edge_rise_counter_program);
    const uint fall_offset = pio_add_program(count_pio, &gate4_edge_fall_counter_program);

    const uint sm_astb_rise = pio_claim_unused_sm(count_pio, true);
    const uint sm_astb_fall = pio_claim_unused_sm(count_pio, true);
    const uint sm_clk_rise = pio_claim_unused_sm(count_pio, true);
    const uint sm_clk_fall = pio_claim_unused_sm(count_pio, true);

    init_edge_counter(count_pio, sm_astb_rise, rise_offset, V30_PIN_ALE, true);
    init_edge_counter(count_pio, sm_astb_fall, fall_offset, V30_PIN_ALE, false);
    init_edge_counter(count_pio, sm_clk_rise, rise_offset, V30_PIN_CLK, true);
    init_edge_counter(count_pio, sm_clk_fall, fall_offset, V30_PIN_CLK, false);

    drive_cpu_input(V30_PIN_RESET, false);

    const uint32_t astb_at_release = gpio_get(V30_PIN_ALE);
    const uint32_t clk_at_release = gpio_get(V30_PIN_CLK);

    pio_enable_sm_mask_in_sync(
        count_pio,
        (1u << sm_astb_rise) |
        (1u << sm_astb_fall) |
        (1u << sm_clk_rise) |
        (1u << sm_clk_fall));

    for (uint i = 0; i < VERIFY_CLOCKS; ++i) {
        clock_step(step_pio, step_sm);
    }

    const uint32_t astb_after_clocks = gpio_get(V30_PIN_ALE);
    const uint32_t clk_after_clocks = gpio_get(V30_PIN_CLK);

    const uint32_t astb_rise_x = read_counter_x(count_pio, sm_astb_rise);
    const uint32_t astb_fall_x = read_counter_x(count_pio, sm_astb_fall);
    const uint32_t clk_rise_x = read_counter_x(count_pio, sm_clk_rise);
    const uint32_t clk_fall_x = read_counter_x(count_pio, sm_clk_fall);

    const uint32_t astb_rises = edge_count_from_x(astb_rise_x);
    const uint32_t astb_falls = edge_count_from_x(astb_fall_x);
    const uint32_t clk_rises = edge_count_from_x(clk_rise_x);
    const uint32_t clk_falls = edge_count_from_x(clk_fall_x);

    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < RESET_CLOCKS; ++i) {
        clock_step(step_pio, step_sm);
    }
    stop_clock_low(step_pio, step_sm);

    printf("=== Minimal edge-count result ===\n");
    printf("RESET hold clocks before release = %u\n", RESET_CLOCKS);
    printf("post-release clocks generated    = %u\n", VERIFY_CLOCKS);
    printf("ASTB level before release        = %lu\n", (unsigned long)astb_before_release);
    printf("ASTB level at release            = %lu\n", (unsigned long)astb_at_release);
    printf("ASTB level after verify clocks   = %lu\n", (unsigned long)astb_after_clocks);
    printf("CLK level before release         = %lu\n", (unsigned long)clk_before_release);
    printf("CLK level at release             = %lu\n", (unsigned long)clk_at_release);
    printf("CLK level after verify clocks    = %lu\n", (unsigned long)clk_after_clocks);
    printf("ASTB rising edges                = %lu\n", (unsigned long)astb_rises);
    printf("ASTB falling edges               = %lu\n", (unsigned long)astb_falls);
    printf("CLK rising edges                 = %lu\n", (unsigned long)clk_rises);
    printf("CLK falling edges                = %lu\n", (unsigned long)clk_falls);

    const bool clk_reference_ok =
        clk_rises == VERIFY_CLOCKS && clk_falls == VERIFY_CLOCKS;

    printf("CLK reference check              = %s\n",
           clk_reference_ok ? "PASS" : "FAIL");

    if (!clk_reference_ok) {
        printf("RESULT: INVALID - edge-counter reference did not count the generated CLK pulses exactly.\n");
    } else {
        printf("RESULT: VALID - ASTB counts are independent of the DMA/address-decoder observer.\n");
    }

    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        printf("Gate 4 ASTB-edge heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
               (unsigned long)heartbeat++);
        fflush(stdout);
        sleep_ms(1000);
    }
}
