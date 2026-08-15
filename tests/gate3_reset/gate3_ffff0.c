#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate3_capture.pio.h"

#define GATE3_CLOCK_HZ          100000u
#define GATE3_RESET_CLOCK_US       200u
#define GATE3_CAPTURE_TIMEOUT_MS    250u
#define GATE3_EXPECTED_ADDRESS   0xFFFF0u

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

static uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0;
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD0)  << 0);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD1)  << 1);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD2)  << 2);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD3)  << 3);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD4)  << 4);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD5)  << 5);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD6)  << 6);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD7)  << 7);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD8)  << 8);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD9)  << 9);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD10) << 10);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD11) << 11);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD12) << 12);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD13) << 13);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD14) << 14);
    value |= (uint16_t)(sample_bit(sample, V30_PIN_AD15) << 15);
    return value;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address;
}

static void init_clock_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate3_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);

    const float divider =
        (float)clock_get_hz(clk_sys) / (2.0f * (float)GATE3_CLOCK_HZ);
    sm_config_set_clkdiv(&c, divider);

    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
}

static void init_capture_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate3_capture_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
}

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 3 RESET / first-fetch capture\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("HAT: original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("CPU: NEC V30 D70116C-8\n\n");
    printf("Test clock: %u Hz on GPIO%u\n", GATE3_CLOCK_HZ, V30_PIN_CLK);
    printf("RESET GPIO%u starts asserted HIGH.\n", V30_PIN_RESET);
    printf("PIO waits for the first ALE HIGH and snapshots GPIO0..GPIO31.\n");
    printf("Expected first physical address: 0x%05X\n\n", GATE3_EXPECTED_ADDRESS);
    printf("No memory data is driven in this test. After the first ALE capture,\n");
    printf("RESET is reasserted and CLK is stopped LOW.\n\n");
    fflush(stdout);
}

int main(void) {
    configure_header_high_z();

    /* Keep the CPU inactive before USB enumeration. */
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);

    stdio_init_all();

    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }

    print_banner();
    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Gate 3 starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO pio = pio0;
    const uint clk_sm = pio_claim_unused_sm(pio, true);
    const uint capture_sm = pio_claim_unused_sm(pio, true);
    const uint clk_offset = pio_add_program(pio, &gate3_clk_program);
    const uint capture_offset = pio_add_program(pio, &gate3_capture_program);

    init_capture_sm(pio, capture_sm, capture_offset);
    init_clock_sm(pio, clk_sm, clk_offset);

    /* Arm capture before any clock reaches the CPU. */
    pio_sm_set_enabled(pio, capture_sm, true);
    pio_sm_set_enabled(pio, clk_sm, true);

    /* RESET must remain asserted for at least four clocks; use a generous margin. */
    sleep_us(GATE3_RESET_CLOCK_US);

    printf("RESET release: HIGH -> LOW\n");
    fflush(stdout);
    drive_cpu_input(V30_PIN_RESET, false);

    const absolute_time_t deadline = make_timeout_time_ms(GATE3_CAPTURE_TIMEOUT_MS);
    while (pio_sm_is_rx_fifo_empty(pio, capture_sm) && !time_reached(deadline)) {
        tight_loop_contents();
    }

    bool captured = !pio_sm_is_rx_fifo_empty(pio, capture_sm);
    uint32_t sample = 0;
    if (captured) {
        sample = pio_sm_get(pio, capture_sm);
    }

    /* Return the CPU to a deterministic inactive state. */
    drive_cpu_input(V30_PIN_RESET, true);
    sleep_us(GATE3_RESET_CLOCK_US);
    stop_clock_low(pio, clk_sm);
    pio_sm_set_enabled(pio, capture_sm, false);

    if (!captured) {
        printf("\nGATE 3 RESULT: FAIL - no ALE capture within %u ms.\n",
               GATE3_CAPTURE_TIMEOUT_MS);
        printf("CPU returned to RESET=HIGH, CLK=LOW.\n");
    } else {
        const uint16_t ad = decode_ad(sample);
        const uint32_t address = decode_address(sample);

        printf("\nFirst ALE capture:\n");
        printf("  raw GPIO snapshot = 0x%08lX\n", (unsigned long)sample);
        printf("  ALE               = %lu\n", (unsigned long)sample_bit(sample, V30_PIN_ALE));
        printf("  IO/M              = %lu\n", (unsigned long)sample_bit(sample, V30_PIN_IOM));
        printf("  BHE               = %lu\n", (unsigned long)sample_bit(sample, V30_PIN_BHE));
        printf("  AD15..AD0         = 0x%04X\n", ad);
        printf("  A19..A16          = 0x%lX\n",
               (unsigned long)((address >> 16) & 0xFu));
        printf("  physical address  = 0x%05lX\n", (unsigned long)address);

        if (address == GATE3_EXPECTED_ADDRESS) {
            printf("\nGATE 3 RESULT: PASS - first fetch is 0xFFFF0.\n");
        } else {
            printf("\nGATE 3 RESULT: FAIL - expected 0xFFFF0, captured 0x%05lX.\n",
                   (unsigned long)address);
        }
        printf("CPU returned to RESET=HIGH, CLK=LOW after capture.\n");
    }

    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 3 halted heartbeat %lu | RESET=1 CLK=0\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
