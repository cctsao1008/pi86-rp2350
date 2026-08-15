#include <stdbool.h>
#include <stdio.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"

static void print_banner(void) {
    printf("\npi86-rp2350\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("CPU target: NEC V30 D70116C-8\n");
    printf("HAT: original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("Current phase: Gate 0 host bring-up\n");
    printf("Next critical V30 milestone: RESET -> first fetch at 0xFFFF0\n");
    printf("Header GPIO range reserved by the original HAT: GPIO%u..GPIO%u\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("V30 CLK GPIO: %u, RESET GPIO: %u\n",
           V30_PIN_CLK,
           V30_PIN_RESET);
    printf("USB CDC connected. Gate 0 firmware is alive.\n\n");
    fflush(stdout);
}

int main(void) {
    stdio_init_all();

    bool was_connected = false;
    uint32_t heartbeat = 0;

    while (true) {
        const bool connected = stdio_usb_connected();

        if (connected && !was_connected) {
            print_banner();
            heartbeat = 0;
        }

        if (connected) {
            printf("Gate 0 heartbeat %lu\n", (unsigned long)heartbeat++);
            fflush(stdout);
        }

        was_connected = connected;
        sleep_ms(1000);
    }
}
