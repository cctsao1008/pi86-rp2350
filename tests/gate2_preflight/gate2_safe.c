#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"

static void configure_hat_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 2 safe preflight firmware\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("HAT target: original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("CPU target: NEC V30 D70116C-8\n");
    printf("\nSAFETY STATE:\n");
    printf("  GPIO%u..GPIO%u are INPUT / high-Z with internal pulls disabled.\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("  V30 CLK GPIO%u is NOT driven.\n", V30_PIN_CLK);
    printf("  V30 RESET GPIO%u is NOT driven.\n", V30_PIN_RESET);
    printf("  AD0..AD15 are NOT driven.\n");
    printf("  This firmware does not perform V30 bus timing.\n");
    printf("\nGate 2 purpose: static electrical preflight only.\n");
    printf("Measure V30 VCC, RESET and CLK with a meter/scope before Gate 3.\n\n");
    fflush(stdout);
}

int main(void) {
    /*
     * Put every Raspberry Pi-compatible HAT GPIO in a non-driving state
     * before starting USB stdio. Gate 2 must never inherit the output-drive
     * behavior of the Gate 1 fixture tests.
     */
    configure_hat_header_high_z();
    stdio_init_all();

    bool was_connected = false;
    uint32_t heartbeat = 0;

    while (true) {
        /* Reassert the safe state continuously at low rate. */
        configure_hat_header_high_z();

        const bool connected = stdio_usb_connected();
        if (connected && !was_connected) {
            print_banner();
            heartbeat = 0;
        }

        if (connected) {
            printf("Gate 2 safe heartbeat %lu | CLK(in)=%u RESET(in)=%u\n",
                   (unsigned long)heartbeat++,
                   gpio_get(V30_PIN_CLK),
                   gpio_get(V30_PIN_RESET));
            fflush(stdout);
        }

        was_connected = connected;
        sleep_ms(1000);
    }
}
