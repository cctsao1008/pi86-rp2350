#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"

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

static void enter_powered_static_state(void) {
    /*
     * First release the entire HAT interface, then actively drive only
     * signals that are inputs to the V30.
     *
     * RESET is asserted HIGH so the CPU remains held in reset.
     * CLK is held LOW; no bus cycles are generated.
     * INTR is held LOW so no interrupt request is presented.
     *
     * Every multiplexed address/data signal and every CPU-output/control
     * signal remains an RP2350 input/high-Z to avoid bus contention.
     */
    configure_header_high_z();
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_INTR, false);
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 2 powered-static test\n");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("HAT: original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("CPU: NEC V30 D70116C-8\n\n");

    printf("POWERED STATIC STATE:\n");
    printf("  RESET GPIO%u = HIGH (asserted)\n", V30_PIN_RESET);
    printf("  CLK   GPIO%u = LOW  (stopped)\n", V30_PIN_CLK);
    printf("  INTR  GPIO%u = LOW\n", V30_PIN_INTR);
    printf("  AD0..AD15 = INPUT / high-Z\n");
    printf("  ALE, IO/M, DT/R, BHE, INTA, A16..A19 = INPUT / high-Z\n");
    printf("  No V30 bus cycle is intentionally generated.\n\n");

    printf("If this remains alive with the HAT installed, Gate 2 powered-static\n");
    printf("bring-up has passed and the next step is Gate 3 RESET/CLK/FFFF0.\n\n");
    fflush(stdout);
}

int main(void) {
    enter_powered_static_state();
    stdio_init_all();

    bool was_connected = false;
    uint32_t heartbeat = 0;

    while (true) {
        /* Reassert only the three intentional CPU-input drives. */
        drive_cpu_input(V30_PIN_CLK, false);
        drive_cpu_input(V30_PIN_RESET, true);
        drive_cpu_input(V30_PIN_INTR, false);

        const bool connected = stdio_usb_connected();
        if (connected && !was_connected) {
            print_banner();
            heartbeat = 0;
        }

        if (connected) {
            printf("Gate 2 powered heartbeat %lu | RESET=%u CLK=%u INTR=%u\n",
                   (unsigned long)heartbeat++,
                   gpio_get(V30_PIN_RESET),
                   gpio_get(V30_PIN_CLK),
                   gpio_get(V30_PIN_INTR));
            fflush(stdout);
        }

        was_connected = connected;
        sleep_ms(1000);
    }
}
