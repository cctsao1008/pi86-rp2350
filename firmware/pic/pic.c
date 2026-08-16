#include <stddef.h>
#include "pic/pic.h"

void pi86_pic_init(pi86_pic_t *pic, uint8_t vector) {
    pic->vector = vector;
    pic->pending = false;
    pic->acknowledge_phase = 0u;
}

void pi86_pic_raise(pi86_pic_t *pic) {
    pic->pending = true;
    pic->acknowledge_phase = 0u;
}

bool pi86_pic_intr_asserted(const pi86_pic_t *pic) {
    return pic->pending;
}

bool pi86_pic_begin_inta(const pi86_pic_t *pic,
                         bool *drive_vector,
                         uint8_t *vector) {
    if (!pic->pending || pic->acknowledge_phase > 1u)
        return false;

    if (drive_vector != NULL)
        *drive_vector = pic->acknowledge_phase == 1u;

    if (vector != NULL)
        *vector = pic->vector;

    return true;
}

bool pi86_pic_end_inta(pi86_pic_t *pic) {
    if (!pic->pending || pic->acknowledge_phase > 1u)
        return false;

    ++pic->acknowledge_phase;
    if (pic->acknowledge_phase == 2u) {
        pic->pending = false;
        pic->acknowledge_phase = 0u;
    }

    return true;
}

uint8_t pi86_pic_acknowledge_phase(const pi86_pic_t *pic) {
    return pic->acknowledge_phase;
}
