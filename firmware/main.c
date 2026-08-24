#include <stdbool.h>
#include <stdio.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "runtime/runtime.h"

int main(void) {
    stdio_init_all();
    pi86_runtime_t runtime;
    pi86_runtime_init(&runtime);

    bool was_connected = false;

    while (true) {
        const bool connected = stdio_usb_connected();

        if (connected && !was_connected) {
            pi86_runtime_print_identity();
            pi86_runtime_print_status(&runtime);
            fflush(stdout);
        }

        was_connected = connected;
        sleep_ms(100);
    }
}
