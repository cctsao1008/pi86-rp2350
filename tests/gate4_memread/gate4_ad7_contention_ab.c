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

#define STEP_PIO_CLOCK_HZ     2000000u
#define PI86_RESET_CLOCKS            8u
#define PI86_MAX_IDLE_STEPS         64u
#define RESET_VECTOR           0xFFFF0u

static const uint8_t ad_gpio[16] = {
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
    for (uint bit = 0; bit < 16u; ++bit) {
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
    }
    return value;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xfffffu;
}

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

static void drive_ad7_only(bool high) {
    const uint32_t mask = 1u << V30_PIN_AD7;
    if (high) {
        sio_hw->gpio_set = mask;
    } else {
        sio_hw->gpio_clr = mask;
    }
    sio_hw->gpio_oe_set = mask;
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

static uint32_t pi86_clk(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 1u);
    (void)pio_sm_get_blocking(pio, sm);
    return sio_hw->gpio_in;
}

static void safe_halt(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

int main(void) {
    configure_header_high_z();
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 4 AD7 contention A/B test\n");
    printf("A: RESET asserted, test RP2350 ability to drive AD7/GPIO%u HIGH.\n",
           V30_PIN_AD7);
    printf("B: pi86-style first memory-read data phase, drive the same AD7 HIGH.\n\n");

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    /* Put the V30 firmly in RESET using the same eight-clock reference cadence. */
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }

    /* A: AD7-only drive while RESET remains asserted. */
    drive_ad7_only(true);
    const uint32_t a_out = sio_hw->gpio_out;
    const uint32_t a_oe = sio_hw->gpio_oe;
    const uint32_t a_pad = sio_hw->gpio_in;
    release_ad_bus();

    /* Begin the known-good pi86 reference sequence. */
    drive_cpu_input(V30_PIN_RESET, false);

    bool saw_ale = false;
    uint32_t t1 = 0u;
    uint steps = 0u;
    for (; steps < PI86_MAX_IDLE_STEPS; ++steps) {
        t1 = pi86_clk(pio, sm);
        if (sample_bit(t1, V30_PIN_ALE)) {
            saw_ale = true;
            break;
        }
    }

    if (!saw_ale) {
        printf("A RESET-held: OUT=%lu OE=%lu PAD=%lu\n",
               (unsigned long)sample_bit(a_out, V30_PIN_AD7),
               (unsigned long)sample_bit(a_oe, V30_PIN_AD7),
               (unsigned long)sample_bit(a_pad, V30_PIN_AD7));
        printf("FAIL: ALE not observed.\n");
        safe_halt(pio, sm);
        while (true) sleep_ms(1000);
    }

    const uint32_t address = decode_address(t1);
    const uint32_t a0 = sample_bit(t1, V30_PIN_AD0);
    const uint32_t bhe = sample_bit(t1, V30_PIN_BHE);
    const uint32_t control = pi86_clk(pio, sm);
    const uint32_t iom = sample_bit(control, V30_PIN_IOM);
    const uint32_t dtr = sample_bit(control, V30_PIN_DTR);
    const uint32_t inta = sample_bit(control, V30_PIN_INTA);

    const bool precondition =
        address == RESET_VECTOR && a0 == 0u && bhe == 0u &&
        iom == 1u && dtr == 0u && inta == 1u;

    /* B: drive exactly the same physical AD7 line HIGH at the data phase. */
    uint32_t b_out = 0u;
    uint32_t b_oe = 0u;
    uint32_t b_pad0 = 0u;
    uint32_t b_pad1 = 0u;
    uint32_t b_pad2 = 0u;

    if (precondition) {
        drive_ad7_only(true);
        b_out = sio_hw->gpio_out;
        b_oe = sio_hw->gpio_oe;
        b_pad0 = sio_hw->gpio_in;
        b_pad1 = pi86_clk(pio, sm);
        b_pad2 = pi86_clk(pio, sm);
        release_ad_bus();
    }

    safe_halt(pio, sm);

    const bool a_cmd_ok =
        sample_bit(a_out, V30_PIN_AD7) == 1u &&
        sample_bit(a_oe, V30_PIN_AD7) == 1u;
    const bool a_pad_ok = sample_bit(a_pad, V30_PIN_AD7) == 1u;

    const bool b_cmd_ok = precondition &&
        sample_bit(b_out, V30_PIN_AD7) == 1u &&
        sample_bit(b_oe, V30_PIN_AD7) == 1u;
    const bool b_pad_ok = precondition &&
        sample_bit(b_pad0, V30_PIN_AD7) == 1u &&
        sample_bit(b_pad1, V30_PIN_AD7) == 1u &&
        sample_bit(b_pad2, V30_PIN_AD7) == 1u;

    printf("=== A: RESET-held AD7 drive ===\n");
    printf("OUT latch = %lu, OE = %lu, PAD = %lu\n",
           (unsigned long)sample_bit(a_out, V30_PIN_AD7),
           (unsigned long)sample_bit(a_oe, V30_PIN_AD7),
           (unsigned long)sample_bit(a_pad, V30_PIN_AD7));
    printf("A command = %s, A pad = %s\n\n",
           a_cmd_ok ? "PASS" : "FAIL",
           a_pad_ok ? "PASS" : "FAIL");

    printf("=== B: active first-read data phase ===\n");
    printf("ALE after CLK() calls = %u\n", steps + 1u);
    printf("address = 0x%05lX, A0/BHE = %lu/%lu\n",
           (unsigned long)address,
           (unsigned long)a0,
           (unsigned long)bhe);
    printf("IO/M DT/R INTA = %lu %lu %lu\n",
           (unsigned long)iom,
           (unsigned long)dtr,
           (unsigned long)inta);
    printf("precondition = %s\n", precondition ? "PASS" : "FAIL");

    if (precondition) {
        printf("OUT latch = %lu, OE = %lu\n",
               (unsigned long)sample_bit(b_out, V30_PIN_AD7),
               (unsigned long)sample_bit(b_oe, V30_PIN_AD7));
        printf("PAD immediate / CLK#1 / CLK#2 = %lu / %lu / %lu\n",
               (unsigned long)sample_bit(b_pad0, V30_PIN_AD7),
               (unsigned long)sample_bit(b_pad1, V30_PIN_AD7),
               (unsigned long)sample_bit(b_pad2, V30_PIN_AD7));
    }
    printf("B command = %s, B pad = %s\n\n",
           b_cmd_ok ? "PASS" : "FAIL",
           b_pad_ok ? "PASS" : "FAIL");

    if (a_pad_ok && !b_pad_ok && b_cmd_ok) {
        printf("RESULT: ACTIVE-PHASE CONTENTION/TIMING SUSPECTED.\n");
        printf("RP2350 can drive AD7 HIGH while RESET is asserted, but not during the read data phase.\n");
    } else if (!a_pad_ok && a_cmd_ok) {
        printf("RESULT: AD7 PHYSICAL DRIVE FAILS EVEN WITH RESET ASSERTED.\n");
        printf("Investigate the physical AD7 path / electrical loading before bus timing.\n");
    } else if (a_pad_ok && b_pad_ok) {
        printf("RESULT: AD7 DRIVE PASSES BOTH CONDITIONS.\n");
    } else {
        printf("RESULT: INCONCLUSIVE - inspect the command/precondition fields above.\n");
    }

    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);
    while (true) sleep_ms(1000);
}
