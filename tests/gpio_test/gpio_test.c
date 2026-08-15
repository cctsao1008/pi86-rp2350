#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"

#define GPIO_TEST_START_DELAY_MS 5000u
#define GPIO_TEST_HIGH_MS         500u
#define GPIO_TEST_GAP_MS          150u
#define GPIO_TEST_SWEEP_GAP_MS   1000u

/* Raspberry Pi 40-pin physical-pin number for each BCM/RP2350 GPIO0..GPIO27. */
static const uint8_t gpio_to_physical_pin[] = {
    27, 28,  3,  5,  7, 29, 31, 26,
    24, 21, 19, 23, 32, 33,  8, 10,
    36, 11, 12, 35, 38, 40, 15, 16,
    18, 22, 37, 13,
};

static void configure_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void configure_header_outputs_low(void) {
    /* Load the output latch before enabling output drive. */
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_put(gpio, 0);
    }

    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_set_dir(gpio, GPIO_OUT);
    }
}

static void set_all_low(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_put(gpio, 0);
    }
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 1 GPIO fixture test\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("Fixture: Pi ALL GPIO TEST BOARD (A)\n");
    printf("Test range: GPIO%u..GPIO%u\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("\nSAFETY: V30 HAT MUST NOT be installed while this firmware is running.\n");
    printf("The header remains high-Z until the countdown completes.\n");
    printf("Each test step drives one GPIO HIGH while every other test GPIO is LOW.\n");
    printf("Observe which fixture LED responds and compare it with the printed GPIO.\n\n");
    fflush(stdout);
}

static bool safety_countdown(void) {
    for (uint32_t remaining = GPIO_TEST_START_DELAY_MS / 1000u;
         remaining > 0;
         --remaining) {
        if (!stdio_usb_connected()) {
            return false;
        }

        printf("Starting GPIO output drive in %lu second%s...\n",
               (unsigned long)remaining,
               remaining == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    return stdio_usb_connected();
}

static bool run_sweep(uint32_t sweep_number) {
    printf("\n--- Sweep %lu ---\n", (unsigned long)sweep_number);
    fflush(stdout);

    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        if (!stdio_usb_connected()) {
            return false;
        }

        set_all_low();
        gpio_put(gpio, 1);

        printf("GPIO%02u HIGH  (40-pin physical pin %u)\n",
               gpio,
               gpio_to_physical_pin[gpio]);
        fflush(stdout);

        sleep_ms(GPIO_TEST_HIGH_MS);
        gpio_put(gpio, 0);
        sleep_ms(GPIO_TEST_GAP_MS);
    }

    set_all_low();
    printf("Sweep %lu complete.\n", (unsigned long)sweep_number);
    fflush(stdout);
    sleep_ms(GPIO_TEST_SWEEP_GAP_MS);
    return true;
}

int main(void) {
    stdio_init_all();
    configure_header_high_z();

    bool session_active = false;
    uint32_t sweep_number = 1;

    while (true) {
        const bool connected = stdio_usb_connected();

        if (!connected) {
            if (session_active) {
                configure_header_high_z();
                session_active = false;
                sweep_number = 1;
            }

            sleep_ms(100);
            continue;
        }

        if (!session_active) {
            configure_header_high_z();
            print_banner();

            if (!safety_countdown()) {
                continue;
            }

            configure_header_outputs_low();
            printf("GPIO output drive enabled. Beginning walking-HIGH test.\n");
            fflush(stdout);
            session_active = true;
        }

        if (!run_sweep(sweep_number++)) {
            configure_header_high_z();
            session_active = false;
            sweep_number = 1;
        }
    }
}
