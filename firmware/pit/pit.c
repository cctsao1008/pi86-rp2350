#include <stddef.h>

#include "pit/pit.h"

void pi86_pit_init(pi86_pit_t *pit) {
    if (pit == NULL)
        return;

    pit->control_word = 0u;
    pit->count_lsb = 0u;
    pit->reload_value = 0u;
    pit->counter = 0u;
    pit->write_state = PI86_PIT_WRITE_EXPECT_LSB;
    pit->programmed = false;
    pit->counting = false;
    pit->output_high = false;
    pit->terminal_count_pending = false;
}

bool pi86_pit_io_write8(pi86_pit_t *pit, uint16_t port, uint8_t value) {
    if (pit == NULL)
        return false;

    if (port == PI86_PIT_CONTROL_PORT) {
        /*
         * Gate 12 accepts exactly:
         *   channel 0
         *   LSB followed by MSB
         *   mode 0 (interrupt on terminal count)
         *   binary counting
         */
        if (value != PI86_PIT_GATE12_CONTROL_WORD)
            return false;

        pit->control_word = value;
        pit->count_lsb = 0u;
        pit->reload_value = 0u;
        pit->counter = 0u;
        pit->write_state = PI86_PIT_WRITE_EXPECT_LSB;
        pit->programmed = false;
        pit->counting = false;
        pit->output_high = false;
        pit->terminal_count_pending = false;
        return true;
    }

    if (port != PI86_PIT_CHANNEL0_PORT ||
        pit->control_word != PI86_PIT_GATE12_CONTROL_WORD) {
        return false;
    }

    if (pit->write_state == PI86_PIT_WRITE_EXPECT_LSB) {
        pit->count_lsb = value;
        pit->write_state = PI86_PIT_WRITE_EXPECT_MSB;
        return true;
    }

    const uint16_t reload = (uint16_t)pit->count_lsb |
                            (uint16_t)((uint16_t)value << 8);

    /* The real PIT interprets zero as 65536; Gate 12 intentionally defers it. */
    if (reload == 0u) {
        pit->write_state = PI86_PIT_WRITE_EXPECT_LSB;
        return false;
    }

    pit->reload_value = reload;
    pit->counter = reload;
    pit->write_state = PI86_PIT_WRITE_EXPECT_LSB;
    pit->programmed = true;
    pit->counting = true;
    pit->output_high = false;
    pit->terminal_count_pending = false;
    return true;
}

void pi86_pit_tick(pi86_pit_t *pit) {
    if (pit == NULL || !pit->counting || pit->counter == 0u)
        return;

    --pit->counter;

    if (pit->counter == 0u) {
        pit->counting = false;
        pit->output_high = true;
        pit->terminal_count_pending = true;
    }
}

bool pi86_pit_take_terminal_count(pi86_pit_t *pit) {
    if (pit == NULL || !pit->terminal_count_pending)
        return false;

    pit->terminal_count_pending = false;
    return true;
}

bool pi86_pit_programmed(const pi86_pit_t *pit) {
    return pit != NULL && pit->programmed;
}

bool pi86_pit_counting(const pi86_pit_t *pit) {
    return pit != NULL && pit->counting;
}

bool pi86_pit_output_high(const pi86_pit_t *pit) {
    return pit != NULL && pit->output_high;
}

uint16_t pi86_pit_reload_value(const pi86_pit_t *pit) {
    return pit != NULL ? pit->reload_value : 0u;
}

uint16_t pi86_pit_counter(const pi86_pit_t *pit) {
    return pit != NULL ? pit->counter : 0u;
}
