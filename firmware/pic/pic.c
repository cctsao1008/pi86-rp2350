#include <stddef.h>
#include "pic/pic.h"

static uint8_t pi86_pic_highest_priority(uint8_t requests) {
    for (uint8_t irq = 0u; irq < PI86_PIC_IRQ_COUNT; ++irq) {
        if ((requests & (uint8_t)(1u << irq)) != 0u)
            return irq;
    }

    return PI86_PIC_NO_IRQ;
}

static uint8_t pi86_pic_eligible_requests(const pi86_pic_t *pic) {
    return (uint8_t)(pic->irr & (uint8_t)~pic->imr);
}

static void pi86_pic_refresh_intr(pi86_pic_t *pic) {
    if (!pic->programmable_mode)
        return;

    if (!pic->initialized || pic->acknowledge_phase != 0u) {
        pic->pending = false;
        return;
    }

    pic->pending = pi86_pic_eligible_requests(pic) != 0u;
}

static void pi86_pic_begin_initialization(pi86_pic_t *pic, uint8_t icw1) {
    pic->programmable_mode = true;
    pic->initialized = false;
    pic->init_state = PI86_PIC_INIT_WAIT_ICW2;
    pic->icw1 = icw1;
    pic->icw3 = 0u;
    pic->icw4 = 0u;
    pic->vector_base = 0u;
    pic->imr = 0u;
    pic->irr = 0u;
    pic->isr = 0u;
    pic->current_irq = PI86_PIC_NO_IRQ;
    pic->current_irq_valid = false;
    pic->pending = false;
    pic->acknowledge_phase = 0u;
}

void pi86_pic_init(pi86_pic_t *pic, uint8_t vector) {
    pic->vector = vector;
    pic->pending = false;
    pic->acknowledge_phase = 0u;

    pic->programmable_mode = false;
    pic->initialized = false;
    pic->init_state = PI86_PIC_INIT_IDLE;
    pic->icw1 = 0u;
    pic->icw3 = 0u;
    pic->icw4 = 0u;
    pic->vector_base = (uint8_t)(vector & 0xF8u);
    pic->imr = 0u;
    pic->irr = 0u;
    pic->isr = 0u;
    pic->current_irq = PI86_PIC_NO_IRQ;
    pic->current_irq_valid = false;
}

void pi86_pic_raise(pi86_pic_t *pic) {
    pic->pending = true;
    pic->acknowledge_phase = 0u;
}

bool pi86_pic_raise_irq(pi86_pic_t *pic, uint8_t irq) {
    if (!pic->programmable_mode || irq >= PI86_PIC_IRQ_COUNT)
        return false;

    pic->irr = (uint8_t)(pic->irr | (uint8_t)(1u << irq));
    pi86_pic_refresh_intr(pic);
    return true;
}

bool pi86_pic_io_write8(pi86_pic_t *pic, uint16_t port, uint8_t value) {
    if (port == PI86_PIC_COMMAND_PORT) {
        if ((value & 0x10u) != 0u) {
            pi86_pic_begin_initialization(pic, value);
            return true;
        }

        if (!pic->programmable_mode || !pic->initialized)
            return false;

        /* Gate 10 supports only non-specific EOI from OCW2. */
        if ((value & 0xE0u) == 0x20u) {
            const uint8_t irq = pi86_pic_highest_priority(pic->isr);
            if (irq != PI86_PIC_NO_IRQ)
                pic->isr = (uint8_t)(pic->isr & (uint8_t)~(uint8_t)(1u << irq));

            pi86_pic_refresh_intr(pic);
            return true;
        }

        return false;
    }

    if (port != PI86_PIC_DATA_PORT || !pic->programmable_mode)
        return false;

    switch (pic->init_state) {
        case PI86_PIC_INIT_WAIT_ICW2:
            pic->vector_base = (uint8_t)(value & 0xF8u);
            pic->init_state = PI86_PIC_INIT_WAIT_ICW3;
            return true;

        case PI86_PIC_INIT_WAIT_ICW3:
            pic->icw3 = value;
            if ((pic->icw1 & 0x01u) != 0u) {
                pic->init_state = PI86_PIC_INIT_WAIT_ICW4;
            } else {
                pic->init_state = PI86_PIC_INIT_IDLE;
                pic->initialized = true;
                pi86_pic_refresh_intr(pic);
            }
            return true;

        case PI86_PIC_INIT_WAIT_ICW4:
            pic->icw4 = value;
            pic->init_state = PI86_PIC_INIT_IDLE;
            pic->initialized = true;
            pi86_pic_refresh_intr(pic);
            return true;

        case PI86_PIC_INIT_IDLE:
            if (!pic->initialized)
                return false;

            pic->imr = value;
            pi86_pic_refresh_intr(pic);
            return true;

        default:
            return false;
    }
}

bool pi86_pic_io_read8(const pi86_pic_t *pic, uint16_t port, uint8_t *value) {
    if (value == NULL || !pic->programmable_mode || !pic->initialized)
        return false;

    if (port == PI86_PIC_COMMAND_PORT) {
        /* Gate 10 default command-port read exposes IRR; OCW3 is deferred. */
        *value = pic->irr;
        return true;
    }

    if (port == PI86_PIC_DATA_PORT) {
        *value = pic->imr;
        return true;
    }

    return false;
}

bool pi86_pic_intr_asserted(const pi86_pic_t *pic) {
    return pic->pending;
}

bool pi86_pic_begin_inta(const pi86_pic_t *pic,
                         bool *drive_vector,
                         uint8_t *vector) {
    if (!pic->programmable_mode) {
        if (!pic->pending || pic->acknowledge_phase > 1u)
            return false;

        if (drive_vector != NULL)
            *drive_vector = pic->acknowledge_phase == 1u;

        if (vector != NULL)
            *vector = pic->vector;

        return true;
    }

    if (!pic->initialized || pic->acknowledge_phase > 1u)
        return false;

    if (pic->acknowledge_phase == 0u) {
        const uint8_t irq = pi86_pic_highest_priority(pi86_pic_eligible_requests(pic));
        if (irq == PI86_PIC_NO_IRQ)
            return false;

        if (drive_vector != NULL)
            *drive_vector = false;

        if (vector != NULL)
            *vector = (uint8_t)(pic->vector_base + irq);

        return true;
    }

    if (!pic->current_irq_valid)
        return false;

    if (drive_vector != NULL)
        *drive_vector = true;

    if (vector != NULL)
        *vector = (uint8_t)(pic->vector_base + pic->current_irq);

    return true;
}

bool pi86_pic_end_inta(pi86_pic_t *pic) {
    if (!pic->programmable_mode) {
        if (!pic->pending || pic->acknowledge_phase > 1u)
            return false;

        ++pic->acknowledge_phase;
        if (pic->acknowledge_phase == 2u) {
            pic->pending = false;
            pic->acknowledge_phase = 0u;
        }

        return true;
    }

    if (!pic->initialized || pic->acknowledge_phase > 1u)
        return false;

    if (pic->acknowledge_phase == 0u) {
        const uint8_t irq = pi86_pic_highest_priority(pi86_pic_eligible_requests(pic));
        if (irq == PI86_PIC_NO_IRQ)
            return false;

        const uint8_t bit = (uint8_t)(1u << irq);
        pic->irr = (uint8_t)(pic->irr & (uint8_t)~bit);
        pic->isr = (uint8_t)(pic->isr | bit);
        pic->current_irq = irq;
        pic->current_irq_valid = true;
        pic->acknowledge_phase = 1u;
        pi86_pic_refresh_intr(pic);
        return true;
    }

    if (!pic->current_irq_valid)
        return false;

    pic->acknowledge_phase = 0u;
    pic->current_irq = PI86_PIC_NO_IRQ;
    pic->current_irq_valid = false;
    pi86_pic_refresh_intr(pic);
    return true;
}

uint8_t pi86_pic_acknowledge_phase(const pi86_pic_t *pic) {
    return pic->acknowledge_phase;
}

bool pi86_pic_initialized(const pi86_pic_t *pic) {
    return pic->programmable_mode && pic->initialized;
}

uint8_t pi86_pic_vector_base(const pi86_pic_t *pic) {
    return pic->vector_base;
}

uint8_t pi86_pic_imr(const pi86_pic_t *pic) {
    return pic->imr;
}

uint8_t pi86_pic_irr(const pi86_pic_t *pic) {
    return pic->irr;
}

uint8_t pi86_pic_isr(const pi86_pic_t *pic) {
    return pic->isr;
}

uint8_t pi86_pic_current_irq(const pi86_pic_t *pic) {
    return pic->current_irq_valid ? pic->current_irq : PI86_PIC_NO_IRQ;
}
