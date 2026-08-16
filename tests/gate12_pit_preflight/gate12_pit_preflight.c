#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pic/pic.h"
#include "pit/pit.h"

#define VECTOR_BASE 0x20u
#define IMR_IRQ0_ENABLED 0xFEu
#define TEST_COUNT 4u

static bool expect(bool condition, const char *label) {
    printf("%-52s = %s\n", label, condition ? "PASS" : "FAIL");
    return condition;
}

static bool initialize_pic(pi86_pic_t *pic) {
    bool ok = true;
    ok &= pi86_pic_io_write8(pic, PI86_PIC_COMMAND_PORT, 0x11u);
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, VECTOR_BASE);
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, 0x00u);
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, 0x01u);
    ok &= pi86_pic_io_write8(pic, PI86_PIC_DATA_PORT, IMR_IRQ0_ENABLED);
    return ok;
}

static bool acknowledge_irq0(pi86_pic_t *pic) {
    bool pass = true;
    bool drive_vector = true;
    uint8_t vector = 0xFFu;

    pass &= pi86_pic_begin_inta(pic, &drive_vector, &vector);
    pass &= !drive_vector;
    pass &= vector == VECTOR_BASE;
    pass &= pi86_pic_end_inta(pic);
    pass &= pi86_pic_current_irq(pic) == 0u;

    drive_vector = false;
    vector = 0xFFu;
    pass &= pi86_pic_begin_inta(pic, &drive_vector, &vector);
    pass &= drive_vector;
    pass &= vector == VECTOR_BASE;
    pass &= pi86_pic_end_inta(pic);

    return pass;
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected())
        sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 12 PIT channel 0 -> PIC IRQ0 core preflight\n");
    printf("Controller-state validation only; physical V30 validation remains required.\n\n");

    pi86_pic_t pic;
    pi86_pic_init(&pic, VECTOR_BASE);

    pi86_pit_t pit;
    pi86_pit_init(&pit);

    bool pass = true;

    pass &= expect(initialize_pic(&pic), "PIC ICW1-4 + IMR programming");
    pass &= expect(pi86_pic_initialized(&pic), "PIC initialized");
    pass &= expect(pi86_pic_imr(&pic) == IMR_IRQ0_ENABLED,
                   "PIC IMR = FEh (IRQ0 enabled)");

    pass &= expect(pi86_pit_io_write8(&pit, PI86_PIT_CONTROL_PORT,
                                      PI86_PIT_GATE12_CONTROL_WORD),
                   "PIT control 43h = 30h accepted");
    pass &= expect(pi86_pit_io_write8(&pit, PI86_PIT_CHANNEL0_PORT,
                                      (uint8_t)(TEST_COUNT & 0xFFu)),
                   "PIT channel 0 LSB accepted");
    pass &= expect(pi86_pit_io_write8(&pit, PI86_PIT_CHANNEL0_PORT,
                                      (uint8_t)(TEST_COUNT >> 8)),
                   "PIT channel 0 MSB accepted");
    pass &= expect(pi86_pit_programmed(&pit), "PIT programmed");
    pass &= expect(pi86_pit_reload_value(&pit) == TEST_COUNT,
                   "PIT reload = 0004h");
    pass &= expect(pi86_pit_counter(&pit) == TEST_COUNT,
                   "PIT counter starts at 0004h");
    pass &= expect(pi86_pit_counting(&pit), "PIT counting active");
    pass &= expect(!pi86_pit_output_high(&pit),
                   "PIT output low before terminal count");
    pass &= expect(!pi86_pic_intr_asserted(&pic),
                   "PIC INTR low before PIT terminal count");

    for (uint16_t tick = 1u; tick < TEST_COUNT; ++tick) {
        pi86_pit_tick(&pit);
        pass &= expect(!pi86_pit_take_terminal_count(&pit),
                       "No terminal-count event before final tick");
        pass &= expect(!pi86_pic_intr_asserted(&pic),
                       "No PIC IRQ0 before terminal count");
    }

    pi86_pit_tick(&pit);
    pass &= expect(pi86_pit_counter(&pit) == 0u,
                   "PIT counter reaches zero");
    pass &= expect(!pi86_pit_counting(&pit),
                   "PIT stops after one-shot terminal count");
    pass &= expect(pi86_pit_output_high(&pit),
                   "PIT output high at terminal count");

    const bool terminal_count = pi86_pit_take_terminal_count(&pit);
    pass &= expect(terminal_count, "Exactly one terminal-count event produced");
    pass &= expect(!pi86_pit_take_terminal_count(&pit),
                   "Terminal-count event is consumable once");

    bool routed_irq0 = false;
    if (terminal_count)
        routed_irq0 = pi86_pic_raise_irq(&pic, 0u);

    pass &= expect(routed_irq0,
                   "Terminal-count event routed through pi86_pic IRQ0");
    pass &= expect(pi86_pic_irr(&pic) == 0x01u,
                   "PIC IRR = 01h after PIT event");
    pass &= expect(pi86_pic_intr_asserted(&pic),
                   "PIC INTR asserted after PIT event");

    pass &= expect(acknowledge_irq0(&pic),
                   "IRQ0 two-cycle acknowledge vectors to 20h");
    pass &= expect(pi86_pic_irr(&pic) == 0x00u,
                   "PIC IRR cleared after IRQ0 acknowledge");
    pass &= expect(pi86_pic_isr(&pic) == 0x01u,
                   "PIC ISR holds IRQ0");

    pass &= expect(pi86_pic_io_write8(&pic, PI86_PIC_COMMAND_PORT, 0x20u),
                   "Non-specific EOI accepted");
    pass &= expect(pi86_pic_isr(&pic) == 0x00u,
                   "PIC ISR cleared after EOI");
    pass &= expect(!pi86_pic_intr_asserted(&pic),
                   "Final PIC INTR deasserted");

    printf("\nGATE 12 PIT CORE PREFLIGHT RESULT: %s\n", pass ? "PASS" : "FAIL");
    printf("Physical V30 OUT 43h/40h -> PIT -> PIC -> INTA/IVT/ISR/EOI/IRET remains required.\n");
    fflush(stdout);

    while (true)
        tight_loop_contents();
}
