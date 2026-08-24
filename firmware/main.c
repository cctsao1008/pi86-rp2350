#include <stdbool.h>
#include <stdio.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"

static void print_banner(void) {
    printf("\npi86-rp2350\n");
    printf("Host board : Waveshare RP2350-PiZero\n");
    printf("CPU        : physical NEC V30 D70116C-8\n");
    printf("Interface  : original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("Model      : RP2350 programmable companion chipset\n");
    printf("Realtime   : PIO/DMA current-cycle bus plane\n");
    printf("Host side  : observe / control / experiment services\n");
    printf("\n");
    printf("Canonical firmware integration shell\n");
    printf("Validated V30 engines currently remain in dedicated test targets.\n");
    printf("Header GPIO range : GPIO%u..GPIO%u\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);
    printf("V30 CLK / RESET   : GPIO%u / GPIO%u\n",
           V30_PIN_CLK,
           V30_PIN_RESET);
    printf("\n");
    fflush(stdout);
}

int main(void) {
    stdio_init_all();

    bool was_connected = false;

    while (true) {
        const bool connected = stdio_usb_connected();

        if (connected && !was_connected) {
            print_banner();
        }

        was_connected = connected;
        sleep_ms(100);
    }
}
