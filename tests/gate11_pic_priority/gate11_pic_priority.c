#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pic/pic.h"

#define VECTOR_BASE 0x20u
#define IMR_IRQ0_IRQ1_ENABLED 0xFCu

static bool expect(bool condition, const char *label) {
    printf("%-44s = %s\n", label, condition ? "PASS" : "FAIL");
    return condition;
}

static bool initialize_pic(pi86_pic_t *pic) {
    bool ok = true;
    ok &= pi86_pic_io_write8(pic, PI86_PIC_COMMAND_PORT, 0x11u); /* ICW1 */
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, VECTOR_BASE); /* ICW2 */
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, 0x00u); /* ICW3 */
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, 0x01u); /* ICW4 */
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, IMR_IRQ0_IRQ1_ENABLED);
    return ok;
}

static bool acknowledge_irq(pi86_pic_t *pic,
                            uint8_t expected_irq,
                            uint8_t expected_vector) {
    bool pass = true;
    bool drive_vector = true;
    uint8_t vector = 0xFFu;

    pass &= pi86_pic_begin_inta(pic, &drive_vector, &vector);
    pass &= !drive_vector;
    pass &= vector == expected_vector;
    pass &= pi86_pic_end_inta(pic);
    pass &= pi86_pic_current_irq(pic) == expected_irq;

    drive_vector = false;
    vector = 0xFFu;
    pass &= pi86_pic_begin_inta(pic, &drive_vector, &vector);
    pass &= drive_vector;
    pass &= vector == expected_vector;
    pass &= pi86_pic_end_inta(pic);
    pass &= pi86_pic_acknowledge_phase(pic) == 0u;
    pass &= pi86_pic_current_irq(pic) == PI86_PIC_NO_IRQ;

    return pass;
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected())
        sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 11 PIC fixed-priority core preflight\n");
    printf("This validates controller state only; it is not the physical V30 Gate 11 PASS.\n\n");

    pi86_pic_t pic;
    pi86_pic_init(&pic, VECTOR_BASE);

    bool pass = true;

    pass &= expect(initialize_pic(&pic), "ICW1-4 + IMR programming");
    pass &= expect(pi86_pic_initialized(&pic), "PIC initialized");
    pass &= expect(pi86_pic_vector_base(&pic) == VECTOR_BASE,
                   "Vector base = 20h");
    pass &= expect(pi86_pic_imr(&pic) == IMR_IRQ0_IRQ1_ENABLED,
                   "IMR = FCh (IRQ0/IRQ1 enabled)");

    pass &= expect(pi86_pic_raise_irq(&pic, 1u), "Raise IRQ1");
    pass &= expect(pi86_pic_raise_irq(&pic, 0u), "Raise IRQ0");
    pass &= expect(pi86_pic_irr(&pic) == 0x03u, "IRR = 03h");
    pass &= expect(pi86_pic_intr_asserted(&pic), "INTR asserted with IRQ0+IRQ1 pending");

    pass &= expect(acknowledge_irq(&pic, 0u, 0x20u),
                   "IRQ0 wins and vectors to 20h");
    pass &= expect(pi86_pic_irr(&pic) == 0x02u,
                   "IRQ1 remains pending in IRR");
    pass &= expect(pi86_pic_isr(&pic) == 0x01u,
                   "ISR holds IRQ0");
    pass &= expect(!pi86_pic_intr_asserted(&pic),
                   "IRQ1 blocked while higher-priority IRQ0 is in service");

    pass &= expect(pi86_pic_io_write8(&pic, PI86_PIC_COMMAND_PORT, 0x20u),
                   "Non-specific EOI for IRQ0");
    pass &= expect(pi86_pic_isr(&pic) == 0x00u,
                   "ISR cleared after IRQ0 EOI");
    pass &= expect(pi86_pic_intr_asserted(&pic),
                   "Pending IRQ1 becomes serviceable after IRQ0 EOI");

    pass &= expect(acknowledge_irq(&pic, 1u, 0x21u),
                   "IRQ1 vectors to 21h");
    pass &= expect(pi86_pic_irr(&pic) == 0x00u,
                   "IRR empty after IRQ1 acknowledge");
    pass &= expect(pi86_pic_isr(&pic) == 0x02u,
                   "ISR holds IRQ1");

    pass &= expect(pi86_pic_io_write8(&pic, PI86_PIC_COMMAND_PORT, 0x20u),
                   "Non-specific EOI for IRQ1");
    pass &= expect(pi86_pic_isr(&pic) == 0x00u,
                   "Final ISR = 00h");
    pass &= expect(!pi86_pic_intr_asserted(&pic),
                   "Final INTR deasserted");

    printf("\nGATE 11 PIC CORE PREFLIGHT RESULT: %s\n", pass ? "PASS" : "FAIL");
    printf("Physical V30 bus validation remains required before Gate 11 can be marked PASS.\n");
    fflush(stdout);

    while (true)
        tight_loop_contents();
}
