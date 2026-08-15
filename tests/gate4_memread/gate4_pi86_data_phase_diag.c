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

#define STEP_PIO_CLOCK_HZ 2000000u
#define PI86_RESET_CLOCKS 8u
#define PI86_MAX_IDLE_STEPS 64u
#define RESET_VECTOR 0xFFFF0u
#define TEST_WORD 0xFEEBu

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];

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
    return address & 0xFFFFFu;
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

static void init_data_luts(void) {
    for (uint32_t value = 0; value < 256u; ++value) {
        uint32_t lo_mask = 0u;
        uint32_t hi_mask = 0u;
        for (uint bit = 0; bit < 8u; ++bit) {
            if ((value >> bit) & 1u) {
                lo_mask |= 1u << ad_gpio[bit];
                hi_mask |= 1u << ad_gpio[bit + 8u];
            }
        }
        data_lo_lut[value] = lo_mask;
        data_hi_lut[value] = hi_mask;
    }
}

static void drive_ad_word(uint16_t word) {
    const uint32_t encoded =
        data_lo_lut[word & 0xffu] |
        data_hi_lut[(word >> 8) & 0xffu];
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = V30_AD_BUS_MASK;
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
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

int main(void) {
    configure_header_high_z();
    init_data_luts();
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 4 pi86 data-phase diagnostic\n");
    printf("Reference sequence: RESET=1, CLK x8, RESET=0, pi86-style ALE loop.\n");
    printf("Requested data word: 0x%04X; focus: AD7 / GPIO%u.\n\n",
           TEST_WORD, V30_PIN_AD7);

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) (void)pi86_clk(pio, sm);
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
        printf("FAIL: ALE not observed within %u pi86 CLK() calls.\n", PI86_MAX_IDLE_STEPS);
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

    printf("ALE after CLK() calls = %u\n", steps + 1u);
    printf("T1 address            = 0x%05lX\n", (unsigned long)address);
    printf("A0/BHE                = %lu/%lu\n", (unsigned long)a0, (unsigned long)bhe);
    printf("IO/M DT/R INTA        = %lu %lu %lu\n",
           (unsigned long)iom, (unsigned long)dtr, (unsigned long)inta);

    if (address != RESET_VECTOR || a0 != 0u || bhe != 0u ||
        iom != 1u || dtr != 0u || inta != 1u) {
        printf("FAIL: reference address/control precondition not met.\n");
        safe_halt(pio, sm);
        while (true) sleep_ms(1000);
    }

    drive_ad_word(TEST_WORD);

    const uint32_t sio_out = sio_hw->gpio_out;
    const uint32_t sio_oe = sio_hw->gpio_oe;
    const uint32_t pad_immediate = sio_hw->gpio_in;
    const uint32_t pad_clk1 = pi86_clk(pio, sm);
    const uint32_t pad_clk2 = pi86_clk(pio, sm);

    const uint16_t out_word = decode_ad(sio_out);
    const uint16_t immediate_word = decode_ad(pad_immediate);
    const uint16_t clk1_word = decode_ad(pad_clk1);
    const uint16_t clk2_word = decode_ad(pad_clk2);

    release_ad_bus();
    safe_halt(pio, sm);

    printf("\n=== Data drive diagnostic ===\n");
    printf("requested word          = 0x%04X\n", TEST_WORD);
    printf("SIO OUT decoded         = 0x%04X\n", out_word);
    printf("AD7 OUT latch           = %lu\n",
           (unsigned long)sample_bit(sio_out, V30_PIN_AD7));
    printf("AD7 OE                  = %lu\n",
           (unsigned long)sample_bit(sio_oe, V30_PIN_AD7));
    printf("pad immediately after OE= 0x%04X  XOR=0x%04X  AD7=%lu\n",
           immediate_word, (uint16_t)(immediate_word ^ TEST_WORD),
           (unsigned long)sample_bit(pad_immediate, V30_PIN_AD7));
    printf("pad after data CLK #1   = 0x%04X  XOR=0x%04X  AD7=%lu\n",
           clk1_word, (uint16_t)(clk1_word ^ TEST_WORD),
           (unsigned long)sample_bit(pad_clk1, V30_PIN_AD7));
    printf("pad after data CLK #2   = 0x%04X  XOR=0x%04X  AD7=%lu\n",
           clk2_word, (uint16_t)(clk2_word ^ TEST_WORD),
           (unsigned long)sample_bit(pad_clk2, V30_PIN_AD7));

    const bool out_ok = out_word == TEST_WORD;
    const bool oe7_ok = sample_bit(sio_oe, V30_PIN_AD7) == 1u;
    const bool pad_ok = immediate_word == TEST_WORD &&
                        clk1_word == TEST_WORD &&
                        clk2_word == TEST_WORD;

    printf("SIO command check       = %s\n", out_ok && oe7_ok ? "PASS" : "FAIL");
    printf("physical pad check      = %s\n", pad_ok ? "PASS" : "FAIL");
    printf("RESULT                  = %s\n",
           out_ok && oe7_ok && pad_ok ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
