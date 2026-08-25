#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "runtime/runtime.h"

enum {
    HOST_COMMAND_CAPACITY = 48,
};

/*
 * Canonical firmware currently exposes CDC, while the structured HID command
 * plane is still being integrated.  Keep this early control path deliberately
 * tiny: one exact, newline-terminated command can request UF2 mode.  Arbitrary
 * CDC text can never release RESET or claim the processor bus.
 */
static void service_host_cdc(pi86_runtime_t *runtime) {
    static char command[HOST_COMMAND_CAPACITY];
    static size_t length;
    static bool overflowed;

    int value;
    while ((value = getchar_timeout_us(0u)) != PICO_ERROR_TIMEOUT) {
        const char ch = (char)value;
        if (ch == '\r' || ch == '\n') {
            if (overflowed) {
                length = 0u;
                overflowed = false;
                printf("PI86 COMMAND ERROR\n");
                fflush(stdout);
                continue;
            }
            if (length == 0u) continue;
            command[length] = '\0';
            const bool print_status =
                strcmp(command, "PI86 STATUS") == 0 ||
                strcmp(command, "status") == 0;
            const bool enter_bootloader =
                strcmp(command, "PI86 BOOTLOADER") == 0 ||
                strcmp(command, "bootloader") == 0 ||
                strcmp(command, "bootsel") == 0;
            length = 0u;
            if (print_status) {
                printf("PI86 STATUS BEGIN\n");
                pi86_runtime_print_status(runtime);
                printf("PI86 STATUS END\n");
                fflush(stdout);
                continue;
            }
            if (enter_bootloader) {
                printf("PI86 BOOTLOADER ACK\n");
                fflush(stdout);
                pi86_runtime_enter_bootloader(runtime);
                return;
            }
            printf("PI86 COMMAND ERROR\n");
            fflush(stdout);
            continue;
        }

        if (length + 1u < sizeof command) {
            command[length++] = ch;
        } else {
            /* Discard an overlong line instead of matching a truncated token. */
            length = 0u;
            overflowed = true;
        }
    }
}

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

        if (connected) service_host_cdc(&runtime);

        was_connected = connected;
        sleep_ms(10);
    }
}
