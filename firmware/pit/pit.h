#ifndef PI86_PIT_H
#define PI86_PIT_H

#include <stdbool.h>
#include <stdint.h>

#define PI86_PIT_CHANNEL0_PORT 0x40u
#define PI86_PIT_CONTROL_PORT  0x43u

/* Gate 12 deliberately supports only channel 0, binary mode 0, LSB/MSB writes. */
#define PI86_PIT_GATE12_CONTROL_WORD 0x30u

typedef enum {
    PI86_PIT_WRITE_EXPECT_LSB = 0,
    PI86_PIT_WRITE_EXPECT_MSB,
} pi86_pit_write_state_t;

typedef struct {
    uint8_t control_word;
    uint8_t count_lsb;
    uint16_t reload_value;
    uint16_t counter;
    pi86_pit_write_state_t write_state;
    bool programmed;
    bool counting;
    bool output_high;
    bool terminal_count_pending;
} pi86_pit_t;

void pi86_pit_init(pi86_pit_t *pit);

/* Gate 12 CPU-visible programming subset: ports 43h and 40h only. */
bool pi86_pit_io_write8(pi86_pit_t *pit, uint16_t port, uint8_t value);

/* Advance the deterministic Gate 12 channel-0 counter by one PIT tick. */
void pi86_pit_tick(pi86_pit_t *pit);

/* Consume one terminal-count event. The caller routes it into pi86_pic IRQ0. */
bool pi86_pit_take_terminal_count(pi86_pit_t *pit);

bool pi86_pit_programmed(const pi86_pit_t *pit);
bool pi86_pit_counting(const pi86_pit_t *pit);
bool pi86_pit_output_high(const pi86_pit_t *pit);
uint16_t pi86_pit_reload_value(const pi86_pit_t *pit);
uint16_t pi86_pit_counter(const pi86_pit_t *pit);

#endif
