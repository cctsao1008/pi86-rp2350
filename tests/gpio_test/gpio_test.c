#include <stdio.h>

#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"

static void set_all_low(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_put(gpio, 0);
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(1500);

    printf("pi86-rp2350 Gate 1 GPIO test\n");
    printf("WARNING: remove the V30 HAT before running this test.\n");
    printf("Testing GPIO%u..GPIO%u on the temporary GPIO test board.\n",
           RP2350_PIZERO_HEADER_GPIO_FIRST,
           RP2350_PIZERO_HEADER_GPIO_LAST);

    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_OUT);
        gpio_put(gpio, 0);
    }

    while (true) {
        for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
             gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
             ++gpio) {
            set_all_low();
            gpio_put(gpio, 1);
            printf("GPIO%u HIGH\n", gpio);
            sleep_ms(200);
            gpio_put(gpio, 0);
            sleep_ms(50);
        }

        printf("GPIO0-GPIO27 sweep complete. Repeating.\n");
        sleep_ms(500);
    }
}
