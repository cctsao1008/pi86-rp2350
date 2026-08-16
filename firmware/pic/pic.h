#ifndef PI86_PIC_H
#define PI86_PIC_H

#include <stdbool.h>
#include <stdint.h>

#define PI86_PIC_COMMAND_PORT 0x20u
#define PI86_PIC_DATA_PORT    0x21u
#define PI86_PIC_IRQ_COUNT       8u
#define PI86_PIC_NO_IRQ        0xFFu

typedef enum {
    PI86_PIC_INIT_IDLE = 0,
    PI86_PIC_INIT_WAIT_ICW2,
    PI86_PIC_INIT_WAIT_ICW3,
    PI86_PIC_INIT_WAIT_ICW4,
} pi86_pic_init_state_t;

typedef struct {
    /* Gate 9R fixed-vector compatibility state. */
    uint8_t vector;
    bool pending;
    uint8_t acknowledge_phase;

    /* Gate 10 programmable 8259A-compatible subset. */
    bool programmable_mode;
    bool initialized;
    pi86_pic_init_state_t init_state;
    uint8_t icw1;
    uint8_t icw3;
    uint8_t icw4;
    uint8_t vector_base;
    uint8_t imr;
    uint8_t irr;
    uint8_t isr;
    uint8_t current_irq;
    bool current_irq_valid;
} pi86_pic_t;

/*
 * Initialize the Gate 9R-compatible fixed-vector backend.
 *
 * This preserves the hardware-validated Gate 9R contract until the CPU
 * explicitly starts an 8259A-style ICW sequence through ports 20h/21h.
 */
void pi86_pic_init(pi86_pic_t *pic, uint8_t vector);

/* Gate 9R compatibility helper: raise the configured fixed-vector request. */
void pi86_pic_raise(pi86_pic_t *pic);

/* Raise one programmable IRQ input. The request is latched in IRR. */
bool pi86_pic_raise_irq(pi86_pic_t *pic, uint8_t irq);

/* CPU-visible 8259A subset at ports 20h/21h. */
bool pi86_pic_io_write8(pi86_pic_t *pic, uint16_t port, uint8_t value);
bool pi86_pic_io_read8(const pi86_pic_t *pic, uint16_t port, uint8_t *value);

bool pi86_pic_intr_asserted(const pi86_pic_t *pic);

/*
 * Begin one interrupt-acknowledge cycle.
 *
 * A request requires two acknowledge cycles. The first cycle does not drive a
 * vector. The second cycle drives the selected 8-bit vector on AD7..AD0.
 *
 * In fixed-vector Gate 9R mode the request clears after acknowledge #2, which
 * preserves the validated regression behavior. In programmable mode the
 * selected request moves IRR -> ISR after acknowledge #1 and remains in ISR
 * until a non-specific EOI is written to port 20h.
 */
bool pi86_pic_begin_inta(const pi86_pic_t *pic,
                         bool *drive_vector,
                         uint8_t *vector);

/* Complete the current acknowledge cycle. Returns false on invalid sequencing. */
bool pi86_pic_end_inta(pi86_pic_t *pic);

uint8_t pi86_pic_acknowledge_phase(const pi86_pic_t *pic);

/* Gate 10 state inspection helpers for deterministic regression checks. */
bool pi86_pic_initialized(const pi86_pic_t *pic);
uint8_t pi86_pic_vector_base(const pi86_pic_t *pic);
uint8_t pi86_pic_imr(const pi86_pic_t *pic);
uint8_t pi86_pic_irr(const pi86_pic_t *pic);
uint8_t pi86_pic_isr(const pi86_pic_t *pic);
uint8_t pi86_pic_current_irq(const pi86_pic_t *pic);

#endif
