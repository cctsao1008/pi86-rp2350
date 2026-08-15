#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"

#define GPIO_TEST_START_DELAY_MS 5000u
#define GPIO_TEST_PHASE_MS       5000u
#define GPIO_TEST_POLL_MS         100u

static void configure_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
        gpio_set_drive_strength(gpio, GPIO_DRIVE_STRENGTH_2MA);
    }
}

static void set_all(bool high) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_put(gpio, high ? 1 : 0);
    }
}

static void configure_header_outputs_low(void) {
    /* Preload LOW before enabling output drive. */
    set_all(false);

    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_set_dir(gpio, GPIO_OUT);
    }
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 1 all-LED blink test\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("Fixture: Pi ALL GPIO TEST BOARD (A)\n");
    printf("GPIO range: GPIO%u..GPIO%u\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("\nSAFETY: V30 HAT MUST NOT be installed while this firmware is running.\n");
    printf("The header remains high-Z until the countdown completes.\n");
    printf("Observed Gate 1 behavior indicates the fixture LEDs are active-HIGH.\n");
    printf("Test pattern: ALL ON for 5 seconds, then ALL OFF for 5 seconds, repeating.\n\n");
    fflush(stdout);
}

static bool wait_while_connected(uint32_t duration_ms) {
    uint32_t elapsed = 0;

    while (elapsed < duration_ms) {
        if (!stdio_usb_connected()) {
            return false;
        }

        sleep_ms(GPIO_TEST_POLL_MS);
        elapsed += GPIO_TEST_POLL_MS;
    }

    return true;
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

        if (!wait_while_connected(1000u)) {
            return false;
        }
    }

    return stdio_usb_connected();
}

int main(void) {
    stdio_init_all();
    configure_header_high_z();

    bool session_active = false;
    uint32_t cycle = 1;

    while (true) {
        if (!stdio_usb_connected()) {
            if (session_active) {
                configure_header_high_z();
                session_active = false;
                cycle = 1;
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
            printf("GPIO output drive enabled.\n");
            fflush(stdout);
            session_active = true;
        }

        printf("Cycle %lu: ALL GPIO LEDs ON  (GPIO0..GPIO27 HIGH)\n",
               (unsigned long)cycle);
        fflush(stdout);
        set_all(true);

        if (!wait_while_connected(GPIO_TEST_PHASE_MS)) {
            configure_header_high_z();
            session_active = false;
            cycle = 1;
            continue;
        }

        printf("Cycle %lu: ALL GPIO LEDs OFF (GPIO0..GPIO27 LOW)\n",
               (unsigned long)cycle);
        fflush(stdout);
        set_all(false);

        if (!wait_while_connected(GPIO_TEST_PHASE_MS)) {
            configure_header_high_z();
            session_active = false;
            cycle = 1;
            continue;
        }

        ++cycle;
    }
}
