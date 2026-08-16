#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "memory/memory.h"
#include "pic/pic.h"
#include "pit/pit.h"
#include "v30/v30_bus.h"
#include "v30/v30_pins.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_CYCLES               360u
#define SUCCESS_HITS_REQUIRED          3u
#define MAX_INTA_FINISH_STEPS         16u

#define PI86_ROM_BASE            0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u

#define IRQ0_LINE                      0u
#define VECTOR_BASE                  0x20u
#define IRQ0_VECTOR                  0x20u
#define IRQ0_IVT_ENTRY_ADDR     (IRQ0_VECTOR * 4u)
#define IRQ0_ISR_ADDR            0xF0100u
#define IRQ0_ISR_OFFSET             0x0100u
#define ISR_SEGMENT                 0xF000u
#define IRQ0_MARKER_ADDR         0x00300u
#define IRQ0_MARKER_VALUE            0xA0u

#define PIT_TEST_COUNT              0x0008u
#define WAIT_LOOP_ADDR           0xF002Cu
#define SUCCESS_LOOP_ADDR        0xF0033u

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static const char *lane_name(v30_bus_lanes_t lanes) {
    switch (lanes) {
        case V30_BUS_LANE_LOW: return "LOW";
        case V30_BUS_LANE_HIGH: return "HIGH";
        case V30_BUS_LANES_WORD: return "WORD";
        default: return "NONE";
    }
}

static bool selected_readback_matches(v30_bus_lanes_t lanes,
                                      uint16_t driven,
                                      uint16_t rb) {
    switch (lanes) {
        case V30_BUS_LANE_LOW:
            return (uint8_t)rb == (uint8_t)driven;
        case V30_BUS_LANE_HIGH:
            return (uint8_t)(rb >> 8) == (uint8_t)(driven >> 8);
        case V30_BUS_LANES_WORD:
            return rb == driven;
        default:
            return false;
    }
}

static void init_test_image(void) {
    for (uint32_t i = 0; i < ROM_SIZE; ++i) rom[i] = 0x90u;
    for (uint32_t i = 0; i < RAM_SIZE; ++i) ram[i] = 0x00u;

    /*
     * F000:0000
     *   CLI
     *   MOV AX,0000h
     *   MOV SS,AX
     *   MOV SP,8000h
     *
     *   MOV AL,11h ; PIC ICW1
     *   OUT 20h,AL
     *   MOV AL,20h ; PIC ICW2 vector base
     *   OUT 21h,AL
     *   MOV AL,00h ; PIC ICW3
     *   OUT 21h,AL
     *   MOV AL,01h ; PIC ICW4
     *   OUT 21h,AL
     *   MOV AL,FEh ; unmask IRQ0 only
     *   OUT 21h,AL
     *
     *   MOV AL,30h ; PIT ch0, LSB/MSB, mode 0, binary
     *   OUT 43h,AL
     *   MOV AL,08h ; count LSB
     *   OUT 40h,AL
     *   MOV AL,00h ; count MSB
     *   OUT 40h,AL
     *
     *   STI
     *   NOP
     *   NOP
     * wait:
     *   CMP BYTE PTR [0300h],A0h
     *   JNE wait
     * success:
     *   JMP success
     */
    static const uint8_t program[] = {
        0xFA,
        0xB8, 0x00, 0x00,
        0x8E, 0xD0,
        0xBC, 0x00, 0x80,

        0xB0, 0x11,
        0xE6, 0x20,
        0xB0, 0x20,
        0xE6, 0x21,
        0xB0, 0x00,
        0xE6, 0x21,
        0xB0, 0x01,
        0xE6, 0x21,
        0xB0, 0xFE,
        0xE6, 0x21,

        0xB0, 0x30,
        0xE6, 0x43,
        0xB0, 0x08,
        0xE6, 0x40,
        0xB0, 0x00,
        0xE6, 0x40,

        0xFB,
        0x90,
        0x90,
        0x80, 0x3E, 0x00, 0x03, 0xA0,
        0x75, 0xF9,
        0xEB, 0xFE,
    };
    for (uint32_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

    /*
     * IRQ0 ISR F000:0100
     *   MOV BYTE PTR [0300h],A0h
     *   MOV AL,20h
     *   OUT 20h,AL
     *   IRET
     */
    static const uint8_t irq0_isr[] = {
        0xC6, 0x06, 0x00, 0x03, 0xA0,
        0xB0, 0x20,
        0xE6, 0x20,
        0xCF,
    };
    for (uint32_t i = 0; i < sizeof(irq0_isr); ++i)
        rom[IRQ0_ISR_OFFSET + i] = irq0_isr[i];

    /* RESET vector: JMP FAR F000:0000. */
    rom[0xFFF0] = 0xEAu;
    rom[0xFFF1] = 0x00u;
    rom[0xFFF2] = 0x00u;
    rom[0xFFF3] = 0x00u;
    rom[0xFFF4] = 0xF0u;
    rom[0xFFF5] = 0x90u;

    /* IVT vector 20h -> F000:0100. */
    ram[IRQ0_IVT_ENTRY_ADDR + 0u] = (uint8_t)(IRQ0_ISR_OFFSET & 0xFFu);
    ram[IRQ0_IVT_ENTRY_ADDR + 1u] = (uint8_t)(IRQ0_ISR_OFFSET >> 8);
    ram[IRQ0_IVT_ENTRY_ADDR + 2u] = (uint8_t)(ISR_SEGMENT & 0xFFu);
    ram[IRQ0_IVT_ENTRY_ADDR + 3u] = (uint8_t)(ISR_SEGMENT >> 8);
}

static bool read_memory_for_cycle(const pi86_memory_t *memory,
                                  const v30_bus_cycle_t *cycle,
                                  uint16_t *driven) {
    if (cycle->lanes == V30_BUS_LANES_WORD)
        return pi86_memory_read16(memory, cycle->address, driven);

    if (cycle->lanes == V30_BUS_LANE_LOW) {
        uint8_t value = 0u;
        if (!pi86_memory_read8(memory, cycle->address, &value)) return false;
        *driven = value;
        return true;
    }

    if (cycle->lanes == V30_BUS_LANE_HIGH) {
        uint8_t value = 0u;
        if (!pi86_memory_read8(memory, cycle->address, &value)) return false;
        *driven = (uint16_t)value << 8;
        return true;
    }

    return false;
}

static bool write_memory_for_cycle(pi86_memory_t *memory,
                                   const v30_bus_cycle_t *cycle,
                                   uint16_t data) {
    if (cycle->lanes == V30_BUS_LANES_WORD)
        return pi86_memory_write16(memory, cycle->address, data);
    if (cycle->lanes == V30_BUS_LANE_LOW)
        return pi86_memory_write8(memory, cycle->address, (uint8_t)data);
    if (cycle->lanes == V30_BUS_LANE_HIGH)
        return pi86_memory_write8(memory, cycle->address, (uint8_t)(data >> 8));
    return false;
}

static bool finish_inta_cycle(v30_bus_t *bus,
                              bool drive_vector,
                              uint8_t vector,
                              uint *steps,
                              uint16_t *last_ad) {
    if (drive_vector)
        v30_bus_drive_data(vector, V30_BUS_LANE_LOW);
    else
        v30_bus_release_ad();

    uint32_t sample = 0u;
    uint count = 0u;
    bool deasserted = false;

    for (; count < MAX_INTA_FINISH_STEPS; ++count) {
        sample = v30_bus_step(bus);
        if (sample_bit(sample, V30_PIN_INTA) != 0u) {
            deasserted = true;
            ++count;
            break;
        }
    }

    if (last_ad != NULL) *last_ad = v30_bus_decode_ad(sample);
    if (steps != NULL) *steps = count;
    v30_bus_release_ad();
    return deasserted;
}

static bool extract_io_write_value(const v30_bus_cycle_t *cycle,
                                   uint16_t data,
                                   uint8_t *value) {
    if (cycle->lanes == V30_BUS_LANE_LOW) {
        *value = (uint8_t)data;
        return true;
    }
    if (cycle->lanes == V30_BUS_LANE_HIGH) {
        *value = (uint8_t)(data >> 8);
        return true;
    }
    return false;
}

static bool advance_pit_and_route_irq(pi86_pit_t *pit,
                                      pi86_pic_t *pic,
                                      bool *saw_terminal_count,
                                      bool *saw_irq0_raise,
                                      uint *terminal_count_events) {
    if (!pi86_pit_programmed(pit) || !pi86_pit_counting(pit))
        return true;

    pi86_pit_tick(pit);
    if (!pi86_pit_take_terminal_count(pit))
        return true;

    *saw_terminal_count = true;
    ++(*terminal_count_events);

    if (!pi86_pic_raise_irq(pic, IRQ0_LINE))
        return false;

    *saw_irq0_raise = true;
    v30_bus_set_intr(pi86_pic_intr_asserted(pic));
    return true;
}

int main(void) {
    v30_bus_prepare_header_high_z();
    init_test_image();

    pi86_memory_t memory;
    pi86_memory_init(&memory,
                     ram, RAM_BASE, RAM_SIZE,
                     rom, PI86_ROM_BASE, ROM_SIZE);

    pi86_pic_t pic;
    pi86_pic_init(&pic, VECTOR_BASE);

    pi86_pit_t pit;
    pi86_pit_init(&pit);

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 12 physical V30 PIT channel 0 -> PIC IRQ0 validation\n");
    printf("V30 must program PIT through OUT 43h/40h; terminal count must reach IRQ0 only through pi86_pic.\n");
    printf("The interrupt must complete real INTA/IVT/ISR/EOI/IRET and return to SUCCESS.\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, PI86_RESET_CLOCKS);

    bool pass = true;
    bool first_read_ok = false;

    bool saw_icw1 = false;
    bool saw_icw2 = false;
    bool saw_icw3 = false;
    bool saw_icw4 = false;
    bool saw_imr = false;

    bool saw_pit_control = false;
    bool saw_pit_lsb = false;
    bool saw_pit_msb = false;
    bool saw_pit_programmed = false;
    bool saw_no_irq_before_terminal = true;
    bool saw_terminal_count = false;
    bool saw_irq0_raise = false;
    uint terminal_count_events = 0u;

    bool saw_irq0_selected = false;
    bool saw_irq0_vector = false;
    bool saw_irq0_ivt_offset = false;
    bool saw_irq0_ivt_segment = false;
    bool saw_irq0_isr_fetch = false;
    bool saw_irq0_marker = false;
    bool saw_irq0_eoi = false;

    uint stack_7ffa_writes = 0u;
    uint stack_7ffc_writes = 0u;
    uint stack_7ffe_writes = 0u;
    uint stack_7ffa_reads = 0u;
    uint stack_7ffc_reads = 0u;
    uint stack_7ffe_reads = 0u;

    uint inta_total = 0u;
    uint inta_first_count = 0u;
    uint inta_second_count = 0u;
    uint success_hits = 0u;
    uint cycles = 0u;

    while (cycles < MAX_BUS_CYCLES && success_hits < SUCCESS_HITS_REQUIRED) {
        v30_bus_cycle_t cycle;
        if (!v30_bus_wait_cycle(&bus, PI86_MAX_IDLE_STEPS, &cycle)) {
            printf("BUS #%u FAIL: ASTB timeout.\n", cycles);
            pass = false;
            break;
        }

        if (cycle.inta_n == 0u) {
            bool drive_vector = false;
            uint8_t vector = 0u;
            const uint8_t phase_before = pi86_pic_acknowledge_phase(&pic);

            if (!pi86_pic_begin_inta(&pic, &drive_vector, &vector)) {
                printf("BUS #%u FAIL: PIC rejected INTA sequencing.\n", cycles);
                pass = false;
                break;
            }

            const uint ack_number = (uint)phase_before + 1u;
            ++inta_total;
            if (ack_number == 1u) ++inta_first_count;
            if (ack_number == 2u) ++inta_second_count;

            uint finish_steps = 0u;
            uint16_t last_ad = 0u;
            if (!finish_inta_cycle(&bus, drive_vector, vector,
                                   &finish_steps, &last_ad)) {
                printf("BUS #%u FAIL: INTA #%u did not deassert.\n", cycles, ack_number);
                pass = false;
                break;
            }

            if (!pi86_pic_end_inta(&pic)) {
                printf("BUS #%u FAIL: PIC failed to complete INTA #%u.\n", cycles, ack_number);
                pass = false;
                break;
            }

            if (ack_number == 1u &&
                pi86_pic_current_irq(&pic) == IRQ0_LINE &&
                pi86_pic_irr(&pic) == 0x00u &&
                pi86_pic_isr(&pic) == 0x01u) {
                saw_irq0_selected = true;
            }

            if (ack_number == 2u && drive_vector && vector == IRQ0_VECTOR)
                saw_irq0_vector = true;

            v30_bus_set_intr(pi86_pic_intr_asserted(&pic));

            printf("BUS #%u INTA #%u: idle=%lu finish_steps=%u %s%02Xh last_AD=%04X IRR=%02X ISR=%02X current=%02X INTR=%u\n",
                   cycles,
                   ack_number,
                   (unsigned long)cycle.idle_steps,
                   finish_steps,
                   drive_vector ? "vector=" : "no-vector ",
                   vector,
                   last_ad,
                   pi86_pic_irr(&pic),
                   pi86_pic_isr(&pic),
                   pi86_pic_current_irq(&pic),
                   pi86_pic_intr_asserted(&pic) ? 1u : 0u);

            ++cycles;
            continue;
        }

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t driven = 0u;
            if (!read_memory_for_cycle(&memory, &cycle, &driven)) {
                printf("BUS #%u FAIL: invalid memory read 0x%05lX lane=%s.\n",
                       cycles, (unsigned long)cycle.address, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            if (cycles == 0u) {
                first_read_ok = cycle.address == RESET_VECTOR_ADDR &&
                                cycle.lanes == V30_BUS_LANES_WORD;
                if (!first_read_ok) {
                    printf("BUS #0 FAIL: reset-vector read mismatch.\n");
                    pass = false;
                    break;
                }
            }

            v30_bus_drive_data(driven, cycle.lanes);
            uint16_t rb1 = 0u;
            uint16_t rb2 = 0u;
            v30_bus_complete_read(&bus, &rb1, &rb2);

            if (!selected_readback_matches(cycle.lanes, driven, rb1) ||
                !selected_readback_matches(cycle.lanes, driven, rb2)) {
                printf("BUS #%u FAIL: memory readback mismatch at 0x%05lX.\n",
                       cycles, (unsigned long)cycle.address);
                pass = false;
                break;
            }

            if (cycle.address == IRQ0_IVT_ENTRY_ADDR && driven == IRQ0_ISR_OFFSET)
                saw_irq0_ivt_offset = true;
            if (cycle.address == IRQ0_IVT_ENTRY_ADDR + 2u && driven == ISR_SEGMENT)
                saw_irq0_ivt_segment = true;
            if (cycle.address == IRQ0_ISR_ADDR) saw_irq0_isr_fetch = true;
            if (cycle.address == 0x07FFAu) ++stack_7ffa_reads;
            if (cycle.address == 0x07FFCu) ++stack_7ffc_reads;
            if (cycle.address == 0x07FFEu) ++stack_7ffe_reads;
            if (cycle.address == SUCCESS_LOOP_ADDR) ++success_hits;

            printf("BUS #%u MEM RD: idle=%lu address=0x%05lX lane=%-4s data=%04X/%04X/%04X%s%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   driven, rb1, rb2,
                   cycle.address == IRQ0_ISR_ADDR ? "  <IRQ0 ISR>" : "",
                   cycle.address == SUCCESS_LOOP_ADDR ? "  <SUCCESS>" : "");
        } else if (cycle.type == V30_BUS_CYCLE_MEM_WRITE) {
            uint16_t d0 = 0u;
            uint16_t d1 = 0u;
            uint16_t d2 = 0u;
            v30_bus_complete_write(&bus, &cycle, &d0, &d1, &d2);

            if (!write_memory_for_cycle(&memory, &cycle, d0)) {
                printf("BUS #%u FAIL: invalid memory write 0x%05lX lane=%s.\n",
                       cycles, (unsigned long)cycle.address, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            if (cycle.lanes == V30_BUS_LANES_WORD) {
                if (cycle.address == 0x07FFAu) ++stack_7ffa_writes;
                if (cycle.address == 0x07FFCu) ++stack_7ffc_writes;
                if (cycle.address == 0x07FFEu) ++stack_7ffe_writes;
            }

            if (cycle.address == IRQ0_MARKER_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)d0 == IRQ0_MARKER_VALUE) {
                saw_irq0_marker = true;
            }

            printf("BUS #%u MEM WR: idle=%lu address=0x%05lX lane=%-4s data=%04X/%04X/%04X%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   d0, d1, d2,
                   cycle.address == IRQ0_MARKER_ADDR ? "  <IRQ0 MARKER>" : "");
        } else if (cycle.type == V30_BUS_CYCLE_IO_WRITE) {
            const uint16_t port = (uint16_t)(cycle.address & 0xFFFFu);
            uint16_t d0 = 0u;
            uint16_t d1 = 0u;
            uint16_t d2 = 0u;
            v30_bus_complete_write(&bus, &cycle, &d0, &d1, &d2);

            uint8_t value = 0u;
            if (!extract_io_write_value(&cycle, d0, &value)) {
                printf("BUS #%u FAIL: byte I/O write expected; port=%04X lane=%s.\n",
                       cycles, port, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            bool accepted = false;
            const uint8_t isr_before = pi86_pic_isr(&pic);

            if (port == PI86_PIC_COMMAND_PORT || port == PI86_PIC_DATA_PORT) {
                accepted = pi86_pic_io_write8(&pic, port, value);

                if (port == PI86_PIC_COMMAND_PORT && value == 0x11u) saw_icw1 = true;
                else if (port == PI86_PIC_DATA_PORT && value == 0x20u && !saw_icw2) saw_icw2 = true;
                else if (port == PI86_PIC_DATA_PORT && value == 0x00u && saw_icw2 && !saw_icw3) saw_icw3 = true;
                else if (port == PI86_PIC_DATA_PORT && value == 0x01u && saw_icw3 && !saw_icw4) saw_icw4 = true;
                else if (port == PI86_PIC_DATA_PORT && value == 0xFEu && saw_icw4) saw_imr = true;

                if (port == PI86_PIC_COMMAND_PORT && value == 0x20u &&
                    (isr_before & 0x01u) != 0u &&
                    (pi86_pic_isr(&pic) & 0x01u) == 0u) {
                    saw_irq0_eoi = true;
                }
            } else if (port == PI86_PIT_CONTROL_PORT || port == PI86_PIT_CHANNEL0_PORT) {
                accepted = pi86_pit_io_write8(&pit, port, value);

                if (port == PI86_PIT_CONTROL_PORT && value == PI86_PIT_GATE12_CONTROL_WORD)
                    saw_pit_control = true;
                else if (port == PI86_PIT_CHANNEL0_PORT && value == (uint8_t)(PIT_TEST_COUNT & 0xFFu) && !saw_pit_lsb)
                    saw_pit_lsb = true;
                else if (port == PI86_PIT_CHANNEL0_PORT && value == (uint8_t)(PIT_TEST_COUNT >> 8) && saw_pit_lsb)
                    saw_pit_msb = true;

                if (pi86_pit_programmed(&pit) &&
                    pi86_pit_reload_value(&pit) == PIT_TEST_COUNT) {
                    saw_pit_programmed = true;
                }
            }

            if (!accepted) {
                printf("BUS #%u FAIL: device rejected I/O write port=%04X value=%02X.\n",
                       cycles, port, value);
                pass = false;
                break;
            }

            v30_bus_set_intr(pi86_pic_intr_asserted(&pic));

            printf("BUS #%u IO  WR: idle=%lu port=%04X lane=%-4s data=%04X/%04X/%04X value=%02X PIC[IMR=%02X IRR=%02X ISR=%02X INTR=%u] PIT[prog=%u count=%04X out=%u]%s%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   port,
                   lane_name(cycle.lanes),
                   d0, d1, d2,
                   value,
                   pi86_pic_imr(&pic),
                   pi86_pic_irr(&pic),
                   pi86_pic_isr(&pic),
                   pi86_pic_intr_asserted(&pic) ? 1u : 0u,
                   pi86_pit_programmed(&pit) ? 1u : 0u,
                   pi86_pit_counter(&pit),
                   pi86_pit_output_high(&pit) ? 1u : 0u,
                   port == PI86_PIT_CONTROL_PORT || port == PI86_PIT_CHANNEL0_PORT ? "  <PIT>" : "",
                   saw_irq0_eoi && port == PI86_PIC_COMMAND_PORT && value == 0x20u ? "  <IRQ0 EOI>" : "");
        } else {
            printf("BUS #%u FAIL: unsupported cycle type=%u address=0x%05lX.\n",
                   cycles,
                   (unsigned)cycle.type,
                   (unsigned long)cycle.address);
            pass = false;
            break;
        }

        if (!saw_terminal_count && pi86_pic_irr(&pic) != 0u) {
            saw_no_irq_before_terminal = false;
        }

        if (!advance_pit_and_route_irq(&pit, &pic,
                                       &saw_terminal_count,
                                       &saw_irq0_raise,
                                       &terminal_count_events)) {
            printf("BUS #%u FAIL: PIT terminal count could not raise PIC IRQ0.\n", cycles);
            pass = false;
            break;
        }

        if (saw_terminal_count && terminal_count_events == 1u && saw_irq0_raise) {
            printf("          >>> PIT terminal count: reload=%04X counter=%04X -> PIC IRR=%02X IMR=%02X INTR=%u <<<\n",
                   pi86_pit_reload_value(&pit),
                   pi86_pit_counter(&pit),
                   pi86_pic_irr(&pic),
                   pi86_pic_imr(&pic),
                   pi86_pic_intr_asserted(&pic) ? 1u : 0u);
            terminal_count_events = 2u; /* suppress duplicate annotation; normalized below */
        }

        ++cycles;
    }

    /* Restore the logical event count after using value 2 as a print latch. */
    const uint observed_terminal_events = saw_terminal_count ? 1u : 0u;
    const uint8_t marker = ram[IRQ0_MARKER_ADDR];

    pass = pass &&
           first_read_ok &&
           saw_icw1 && saw_icw2 && saw_icw3 && saw_icw4 && saw_imr &&
           pi86_pic_initialized(&pic) &&
           pi86_pic_vector_base(&pic) == VECTOR_BASE &&
           pi86_pic_imr(&pic) == 0xFEu &&
           saw_pit_control && saw_pit_lsb && saw_pit_msb && saw_pit_programmed &&
           saw_no_irq_before_terminal && saw_terminal_count && saw_irq0_raise &&
           observed_terminal_events == 1u &&
           inta_total == 2u && inta_first_count == 1u && inta_second_count == 1u &&
           saw_irq0_selected && saw_irq0_vector &&
           saw_irq0_ivt_offset && saw_irq0_ivt_segment && saw_irq0_isr_fetch &&
           saw_irq0_marker && marker == IRQ0_MARKER_VALUE && saw_irq0_eoi &&
           stack_7ffa_writes >= 1u && stack_7ffc_writes >= 1u && stack_7ffe_writes >= 1u &&
           stack_7ffa_reads >= 1u && stack_7ffc_reads >= 1u && stack_7ffe_reads >= 1u &&
           pi86_pic_irr(&pic) == 0u && pi86_pic_isr(&pic) == 0u &&
           !pi86_pic_intr_asserted(&pic) &&
           success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles                  = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector WORD read         = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("PIC ICW1 / ICW2 / ICW3 / ICW4       = %s / %s / %s / %s\n",
           saw_icw1 ? "YES" : "NO",
           saw_icw2 ? "YES" : "NO",
           saw_icw3 ? "YES" : "NO",
           saw_icw4 ? "YES" : "NO");
    printf("PIC IMR / vector base                = %02Xh / %02Xh\n",
           pi86_pic_imr(&pic), pi86_pic_vector_base(&pic));
    printf("PIT OUT 43h control 30h              = %s\n", saw_pit_control ? "YES" : "NO");
    printf("PIT OUT 40h LSB/MSB                  = %s / %s\n",
           saw_pit_lsb ? "YES" : "NO", saw_pit_msb ? "YES" : "NO");
    printf("PIT programmed / reload              = %s / %04Xh\n",
           saw_pit_programmed ? "YES" : "NO", pi86_pit_reload_value(&pit));
    printf("No IRQ0 before PIT terminal count    = %s\n", saw_no_irq_before_terminal ? "YES" : "NO");
    printf("PIT terminal count / IRQ0 routed     = %s / %s\n",
           saw_terminal_count ? "YES" : "NO", saw_irq0_raise ? "YES" : "NO");
    printf("INTA cycles                          = %u total (%u first / %u second)\n",
           inta_total, inta_first_count, inta_second_count);
    printf("IRQ0 selected / vector 20h           = %s / %s\n",
           saw_irq0_selected ? "YES" : "NO", saw_irq0_vector ? "YES" : "NO");
    printf("IRQ0 IVT offset/segment              = %s / %s\n",
           saw_irq0_ivt_offset ? "YES" : "NO", saw_irq0_ivt_segment ? "YES" : "NO");
    printf("IRQ0 ISR fetch / marker A0h          = %s / %s\n",
           saw_irq0_isr_fetch ? "YES" : "NO", saw_irq0_marker ? "YES" : "NO");
    printf("IRQ0 EOI                             = %s\n", saw_irq0_eoi ? "YES" : "NO");
    printf("Stack writes 7FFA/7FFC/7FFE          = %u / %u / %u\n",
           stack_7ffa_writes, stack_7ffc_writes, stack_7ffe_writes);
    printf("IRET stack reads 7FFA/7FFC/7FFE      = %u / %u / %u\n",
           stack_7ffa_reads, stack_7ffc_reads, stack_7ffe_reads);
    printf("Final IRR / ISR / INTR               = %02Xh / %02Xh / %u\n",
           pi86_pic_irr(&pic), pi86_pic_isr(&pic),
           pi86_pic_intr_asserted(&pic) ? 1u : 0u);
    printf("Success-loop hits F0033              = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("GATE 12 PHYSICAL V30 RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_shutdown(&bus);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
