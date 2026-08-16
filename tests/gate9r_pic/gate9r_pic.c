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
#define MAX_BUS_CYCLES               256u
#define SUCCESS_HITS_REQUIRED          3u
#define MAX_INTA_FINISH_STEPS         16u

#define ROM_BASE                 0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u

#define INTERRUPT_VECTOR              0x20u
#define IVT_ENTRY_ADDR          (INTERRUPT_VECTOR * 4u)
#define ISR_ADDR                 0xF0100u
#define ISR_OFFSET                  0x0100u
#define ISR_SEGMENT                 0xF000u
#define ISR_MARKER_ADDR          0x00300u
#define ISR_MARKER_VALUE             0x5Au

#define WAIT_LOOP_ADDR           0xF000Cu
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

    /* CLI; MOV AX,0; MOV SS,AX; MOV SP,8000h; STI; NOP; NOP; JMP $ */
    static const uint8_t program[] = {
        0xFA,
        0xB8, 0x00, 0x00,
        0x8E, 0xD0,
        0xBC, 0x00, 0x80,
        0xFB,
        0x90,
        0x90,
        0xEB, 0xFE,
    };
    for (uint32_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

    /* ISR F000:0100: MOV BYTE PTR [0300],5A; JMP FAR F000:0040 */
    static const uint8_t isr[] = {
        0xC6, 0x06, 0x00, 0x03, 0x5A,
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
                     rom, ROM_BASE, ROM_SIZE);

    pi86_pic_t pic;
    pi86_pic_init(&pic, INTERRUPT_VECTOR);

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 9R reusable PIC backend regression test\n");
    printf("Regression target: reproduce Gate 9 through reusable pi86_pic state.\n");
    printf("PIC scope: one pending IRQ + two INTA cycles + vector on INTA #2 only.\n");
    printf("No ICW/OCW, masks, priority, EOI, or 8259 I/O-port programming yet.\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, PI86_RESET_CLOCKS);

    bool pass = true;
    bool first_read_ok = false;
    bool irq_raised = false;
    bool irq_cleared = false;
    bool saw_inta1 = false;
    bool saw_inta2 = false;
    bool saw_ivt_offset_read = false;
    bool saw_ivt_segment_read = false;
    bool saw_isr_fetch = false;
    bool saw_isr_marker_write = false;
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

            v30_bus_set_intr(pi86_pic_intr_asserted(&pic));
            if (ack_number == 2u && !pi86_pic_intr_asserted(&pic))
                irq_cleared = true;

            printf("BUS #%u INTA #%u: idle=%lu finish_steps=%u %s last_AD=%04X PIC_INTR=%u\n",
                   cycles,
                   ack_number,
                   (unsigned long)cycle.idle_steps,
                   finish_steps,
                   drive_vector ? "vector=20h" : "no-vector",
                   last_ad,
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
                    pi86_pic_raise(&pic);
                    v30_bus_set_intr(pi86_pic_intr_asserted(&pic));
                    irq_raised = true;
                    printf("          >>> PIC IRQ raised after wait-loop hit %u <<<\n", wait_hits);
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
        } else {
            printf("BUS #%u FAIL: unsupported cycle type=%u.\n",
                   cycles, (unsigned)cycle.type);
            pass = false;
            break;
        }

        ++cycles;
    }

    uint8_t marker = 0u;
    const bool marker_ok =
        pi86_memory_read8(&memory, ISR_MARKER_ADDR, &marker) &&
        marker == ISR_MARKER_VALUE;

    pass = pass &&
           first_read_ok &&
           irq_raised && irq_cleared &&
           inta_count == 2u && saw_inta1 && saw_inta2 &&
           stack_seen_7ffa && stack_seen_7ffc && stack_seen_7ffe &&
           saw_ivt_offset_read && saw_ivt_segment_read &&
           saw_isr_fetch && saw_isr_marker_write && marker_ok &&
           !saw_fail_loop &&
           success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles             = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector WORD read    = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("PIC IRQ raised / cleared        = %s / %s\n",
           irq_raised ? "YES" : "NO", irq_cleared ? "YES" : "NO");
    printf("INTAK cycles observed           = %u/2\n", inta_count);
    printf("INTAK #1 / #2                   = %s / %s\n",
           saw_inta1 ? "YES" : "NO", saw_inta2 ? "YES" : "NO");
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
    printf("Success-loop hits F0040         = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("Fail-loop F0050 observed        = %s\n", saw_fail_loop ? "YES" : "NO");
    printf("GATE 9R PIC REGRESSION RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
