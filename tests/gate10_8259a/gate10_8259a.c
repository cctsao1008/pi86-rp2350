#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "memory/memory.h"
#include "pic/pic.h"
#include "v30/v30_bus.h"
#include "v30/v30_pins.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_CYCLES               320u
#define SUCCESS_HITS_REQUIRED          3u
#define MAX_INTA_FINISH_STEPS         16u

#define PI86_ROM_BASE            0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u

#define IRQ_LINE                       0u
#define VECTOR_BASE                  0x20u
#define INTERRUPT_VECTOR             0x20u
#define IVT_ENTRY_ADDR          (INTERRUPT_VECTOR * 4u)
#define ISR_ADDR                 0xF0100u
#define ISR_OFFSET                  0x0100u
#define ISR_SEGMENT                 0xF000u
#define ISR_MARKER_ADDR          0x00300u
#define ISR_MARKER_VALUE             0x5Au

#define WAIT_LOOP_ADDR           0xF0020u
#define SUCCESS_LOOP_ADDR        0xF0040u
#define FAIL_LOOP_ADDR           0xF0050u

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
     *   MOV AL,11h ; ICW1
     *   OUT 20h,AL
     *   MOV AL,20h ; ICW2 vector base
     *   OUT 21h,AL
     *   MOV AL,00h ; ICW3 gate-local master value
     *   OUT 21h,AL
     *   MOV AL,01h ; ICW4 8086/8088 mode
     *   OUT 21h,AL
     *   MOV AL,FEh ; unmask IRQ0 only
     *   OUT 21h,AL
     *   STI
     *   NOP
     *   NOP
     * wait:
     *   JMP wait
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
        0xFB,
        0x90,
        0x90,
        0xEB, 0xFE,
    };
    for (uint32_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

    /*
     * ISR F000:0100
     *   MOV BYTE PTR [0300],5Ah
     *   MOV AL,20h
     *   OUT 20h,AL       ; non-specific EOI
     *   JMP FAR F000:0040
     */
    static const uint8_t isr[] = {
        0xC6, 0x06, 0x00, 0x03, 0x5A,
        0xB0, 0x20,
        0xE6, 0x20,
        0xEA, 0x40, 0x00, 0x00, 0xF0,
    };
    for (uint32_t i = 0; i < sizeof(isr); ++i) rom[ISR_OFFSET + i] = isr[i];

    rom[0x0040] = 0xEBu;
    rom[0x0041] = 0xFEu;
    rom[0x0050] = 0xEBu;
    rom[0x0051] = 0xFEu;

    /* RESET vector: JMP FAR F000:0000. */
    rom[0xFFF0] = 0xEAu;
    rom[0xFFF1] = 0x00u;
    rom[0xFFF2] = 0x00u;
    rom[0xFFF3] = 0x00u;
    rom[0xFFF4] = 0xF0u;
    rom[0xFFF5] = 0x90u;

    /* IVT vector 20h -> F000:0100. */
    ram[IVT_ENTRY_ADDR + 0u] = (uint8_t)(ISR_OFFSET & 0xFFu);
    ram[IVT_ENTRY_ADDR + 1u] = (uint8_t)(ISR_OFFSET >> 8);
    ram[IVT_ENTRY_ADDR + 2u] = (uint8_t)(ISR_SEGMENT & 0xFFu);
    ram[IVT_ENTRY_ADDR + 3u] = (uint8_t)(ISR_SEGMENT >> 8);
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

int main(void) {
    v30_bus_prepare_header_high_z();
    init_test_image();

    pi86_memory_t memory;
    pi86_memory_init(&memory,
                     ram, RAM_BASE, RAM_SIZE,
                     rom, PI86_ROM_BASE, ROM_SIZE);

    pi86_pic_t pic;
    pi86_pic_init(&pic, INTERRUPT_VECTOR);

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 10 programmable 8259A-compatible PIC test\n");
    printf("CPU programs ports 20h/21h, unmasks IRQ0, services vector 20h, and sends EOI.\n");
    printf("Scope: ICW1-4, IMR, IRR, ISR, fixed priority, IRQ0, two INTA cycles, EOI.\n");
    printf("PIT and advanced 8259A modes are intentionally excluded.\n\n");
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
    bool irq_raised = false;
    bool saw_inta1 = false;
    bool saw_inta2 = false;
    bool saw_vector20 = false;
    bool saw_irr_to_isr = false;
    bool saw_ivt_offset_read = false;
    bool saw_ivt_segment_read = false;
    bool saw_isr_fetch = false;
    bool saw_isr_marker_write = false;
    bool saw_eoi = false;
    bool saw_fail_loop = false;
    bool stack_seen_7ffa = false;
    bool stack_seen_7ffc = false;
    bool stack_seen_7ffe = false;
    uint wait_hits = 0u;
    uint inta_count = 0u;
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
            ++inta_count;
            if (ack_number == 1u) saw_inta1 = true;
            if (ack_number == 2u) saw_inta2 = true;
            if (ack_number == 2u && drive_vector && vector == INTERRUPT_VECTOR)
                saw_vector20 = true;

            uint finish_steps = 0u;
            uint16_t last_ad = 0u;
            if (!finish_inta_cycle(&bus,
                                   drive_vector,
                                   vector,
                                   &finish_steps,
                                   &last_ad)) {
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
                (pi86_pic_irr(&pic) & 0x01u) == 0u &&
                (pi86_pic_isr(&pic) & 0x01u) != 0u &&
                pi86_pic_current_irq(&pic) == IRQ_LINE) {
                saw_irr_to_isr = true;
            }

            v30_bus_set_intr(pi86_pic_intr_asserted(&pic));

            printf("BUS #%u INTA #%u: idle=%lu finish_steps=%u %s%02Xh last_AD=%04X IRR=%02X ISR=%02X INTR=%u\n",
                   cycles,
                   ack_number,
                   (unsigned long)cycle.idle_steps,
                   finish_steps,
                   drive_vector ? "vector=" : "no-vector ",
                   vector,
                   last_ad,
                   pi86_pic_irr(&pic),
                   pi86_pic_isr(&pic),
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

            if (cycle.address == WAIT_LOOP_ADDR) {
                ++wait_hits;
                if (!irq_raised && wait_hits >= 2u) {
                    if (!pi86_pic_initialized(&pic) ||
                        pi86_pic_vector_base(&pic) != VECTOR_BASE ||
                        pi86_pic_imr(&pic) != 0xFEu) {
                        printf("BUS #%u FAIL: PIC not ready before IRQ0 raise: init=%u base=%02X IMR=%02X.\n",
                               cycles,
                               pi86_pic_initialized(&pic) ? 1u : 0u,
                               pi86_pic_vector_base(&pic),
                               pi86_pic_imr(&pic));
                        pass = false;
                        break;
                    }

                    if (!pi86_pic_raise_irq(&pic, IRQ_LINE)) {
                        printf("BUS #%u FAIL: PIC rejected IRQ0 raise.\n", cycles);
                        pass = false;
                        break;
                    }

                    v30_bus_set_intr(pi86_pic_intr_asserted(&pic));
                    irq_raised = true;
                    printf("          >>> IRQ0 raised: IRR=%02X IMR=%02X INTR=%u <<<\n",
                           pi86_pic_irr(&pic),
                           pi86_pic_imr(&pic),
                           pi86_pic_intr_asserted(&pic) ? 1u : 0u);
                }
            }

            if (cycle.address == IVT_ENTRY_ADDR && driven == ISR_OFFSET)
                saw_ivt_offset_read = true;
            if (cycle.address == IVT_ENTRY_ADDR + 2u && driven == ISR_SEGMENT)
                saw_ivt_segment_read = true;
            if (cycle.address == ISR_ADDR) saw_isr_fetch = true;
            if (cycle.address == SUCCESS_LOOP_ADDR) ++success_hits;
            if (cycle.address == FAIL_LOOP_ADDR) saw_fail_loop = true;

            printf("BUS #%u MEM RD: idle=%lu address=0x%05lX lane=%-4s data=%04X/%04X/%04X%s%s%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   driven, rb1, rb2,
                   cycle.address == ISR_ADDR ? "  <ISR>" : "",
                   cycle.address == SUCCESS_LOOP_ADDR ? "  <SUCCESS>" : "",
                   cycle.address == FAIL_LOOP_ADDR ? "  <FAIL>" : "");
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
                if (cycle.address == 0x07FFAu) stack_seen_7ffa = true;
                if (cycle.address == 0x07FFCu) stack_seen_7ffc = true;
                if (cycle.address == 0x07FFEu) stack_seen_7ffe = true;
            }

            if (cycle.address == ISR_MARKER_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)d0 == ISR_MARKER_VALUE) {
                saw_isr_marker_write = true;
            }

            printf("BUS #%u MEM WR: idle=%lu address=0x%05lX lane=%-4s data=%04X/%04X/%04X%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   d0, d1, d2,
                   cycle.address == ISR_MARKER_ADDR ? "  <ISR MARKER>" : "");
        } else if (cycle.type == V30_BUS_CYCLE_IO_WRITE) {
            const uint16_t port = (uint16_t)(cycle.address & 0xFFFFu);
            uint16_t d0 = 0u;
            uint16_t d1 = 0u;
            uint16_t d2 = 0u;
            v30_bus_complete_write(&bus, &cycle, &d0, &d1, &d2);

            uint8_t value = 0u;
            if (cycle.lanes == V30_BUS_LANE_LOW) {
                value = (uint8_t)d0;
            } else if (cycle.lanes == V30_BUS_LANE_HIGH) {
                value = (uint8_t)(d0 >> 8);
            } else {
                printf("BUS #%u FAIL: Gate 10 supports byte PIC I/O writes only; port=%04X lane=%s.\n",
                       cycles, port, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            const uint8_t isr_before = pi86_pic_isr(&pic);
            if (!pi86_pic_io_write8(&pic, port, value)) {
                printf("BUS #%u FAIL: PIC rejected I/O write port=%04X value=%02X.\n",
                       cycles, port, value);
                pass = false;
                break;
            }

            if (port == PI86_PIC_COMMAND_PORT && value == 0x11u) saw_icw1 = true;
            else if (port == PI86_PIC_DATA_PORT && value == 0x20u && !saw_icw2) saw_icw2 = true;
            else if (port == PI86_PIC_DATA_PORT && value == 0x00u && saw_icw2 && !saw_icw3) saw_icw3 = true;
            else if (port == PI86_PIC_DATA_PORT && value == 0x01u && saw_icw3 && !saw_icw4) saw_icw4 = true;
            else if (port == PI86_PIC_DATA_PORT && value == 0xFEu && saw_icw4) saw_imr = true;

            if (port == PI86_PIC_COMMAND_PORT && value == 0x20u &&
                (isr_before & 0x01u) != 0u &&
                (pi86_pic_isr(&pic) & 0x01u) == 0u) {
                saw_eoi = true;
            }

            v30_bus_set_intr(pi86_pic_intr_asserted(&pic));

            printf("BUS #%u IO  WR: idle=%lu port=%04X lane=%-4s data=%04X/%04X/%04X value=%02X init=%u base=%02X IMR=%02X IRR=%02X ISR=%02X%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   port,
                   lane_name(cycle.lanes),
                   d0, d1, d2,
                   value,
                   pi86_pic_initialized(&pic) ? 1u : 0u,
                   pi86_pic_vector_base(&pic),
                   pi86_pic_imr(&pic),
                   pi86_pic_irr(&pic),
                   pi86_pic_isr(&pic),
                   saw_eoi && port == PI86_PIC_COMMAND_PORT && value == 0x20u ? "  <EOI>" : "");
        } else {
            printf("BUS #%u FAIL: unsupported cycle type=%u address=0x%05lX.\n",
                   cycles,
                   (unsigned)cycle.type,
                   (unsigned long)cycle.address);
            pass = false;
            break;
        }

        ++cycles;
    }

    const uint8_t marker = ram[ISR_MARKER_ADDR];

    pass = pass &&
           first_read_ok &&
           saw_icw1 && saw_icw2 && saw_icw3 && saw_icw4 && saw_imr &&
           pi86_pic_initialized(&pic) &&
           pi86_pic_vector_base(&pic) == VECTOR_BASE &&
           pi86_pic_imr(&pic) == 0xFEu &&
           irq_raised &&
           inta_count == 2u && saw_inta1 && saw_inta2 && saw_vector20 &&
           saw_irr_to_isr &&
           stack_seen_7ffa && stack_seen_7ffc && stack_seen_7ffe &&
           saw_ivt_offset_read && saw_ivt_segment_read &&
           saw_isr_fetch && saw_isr_marker_write && marker == ISR_MARKER_VALUE &&
           saw_eoi && pi86_pic_isr(&pic) == 0u &&
           !saw_fail_loop && success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles             = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector WORD read    = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("ICW1 / ICW2 / ICW3 / ICW4      = %s / %s / %s / %s\n",
           saw_icw1 ? "YES" : "NO",
           saw_icw2 ? "YES" : "NO",
           saw_icw3 ? "YES" : "NO",
           saw_icw4 ? "YES" : "NO");
    printf("PIC initialized / vector base   = %s / %02Xh\n",
           pi86_pic_initialized(&pic) ? "YES" : "NO",
           pi86_pic_vector_base(&pic));
    printf("IMR programmed                  = %02Xh (expected FEh)\n", pi86_pic_imr(&pic));
    printf("IRQ0 raised                     = %s\n", irq_raised ? "YES" : "NO");
    printf("INTAK cycles observed           = %u/2\n", inta_count);
    printf("INTAK #1 / #2                   = %s / %s\n",
           saw_inta1 ? "YES" : "NO", saw_inta2 ? "YES" : "NO");
    printf("INTAK #2 vector                 = %s\n", saw_vector20 ? "20h" : "NOT 20h");
    printf("IRR -> ISR on INTA #1           = %s\n", saw_irr_to_isr ? "YES" : "NO");
    printf("Stack saves 7FFA/7FFC/7FFE      = %s / %s / %s\n",
           stack_seen_7ffa ? "YES" : "NO",
           stack_seen_7ffc ? "YES" : "NO",
           stack_seen_7ffe ? "YES" : "NO");
    printf("IVT offset/segment reads        = %s / %s\n",
           saw_ivt_offset_read ? "YES" : "NO",
           saw_ivt_segment_read ? "YES" : "NO");
    printf("ISR fetch F0100                 = %s\n", saw_isr_fetch ? "YES" : "NO");
    printf("ISR marker [0300]               = %02X (expected %02X)\n",
           marker, ISR_MARKER_VALUE);
    printf("Non-specific EOI                = %s\n", saw_eoi ? "YES" : "NO");
    printf("Final ISR register              = %02Xh (expected 00h)\n", pi86_pic_isr(&pic));
    printf("Success-loop hits F0040         = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("Fail-loop F0050 observed        = %s\n", saw_fail_loop ? "YES" : "NO");
    printf("GATE 10 8259A RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_shutdown(&bus);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
