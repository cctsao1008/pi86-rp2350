#ifndef PI86_PIC_H
#define PI86_PIC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t vector;
    bool pending;
    uint8_t acknowledge_phase;
} pi86_pic_t;

void pi86_pic_init(pi86_pic_t *pic, uint8_t vector);
void pi86_pic_raise(pi86_pic_t *pic);
bool pi86_pic_intr_asserted(const pi86_pic_t *pic);

/*
 * Begin one interrupt-acknowledge cycle.
 *
 * A pending interrupt requires two acknowledge cycles. The first cycle does
 * not provide a vector. The second cycle provides the configured 8-bit vector
 * on AD7..AD0. The request remains pending until pi86_pic_end_inta() completes
 * the second acknowledge cycle.
 */
bool pi86_pic_begin_inta(const pi86_pic_t *pic,
                         bool *drive_vector,
                         uint8_t *vector);

/* Complete the current acknowledge cycle. Returns false on invalid sequencing. */
bool pi86_pic_end_inta(pi86_pic_t *pic);

uint8_t pi86_pic_acknowledge_phase(const pi86_pic_t *pic);

#endif
