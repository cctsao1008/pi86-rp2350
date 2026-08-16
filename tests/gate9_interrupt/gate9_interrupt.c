#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "memory/memory.h"
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

#define STACK_TOP                  0x8000u
#define STACK_LOW_EXPECTED         0x7FFAu
#define STACK_HIGH_EXPECTED        0x7FFEu

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
     *   MOV AX,0000
     *   MOV SS,AX
     *   MOV SP,8000
     *   STI
     *   NOP
     *   NOP
     * wait:
     *   JMP SHORT wait
     *
     * The host asserts INT only after the wait loop is observed repeatedly.
     * This keeps interrupt enable and interrupt delivery as separate evidence.
     */
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

    /*
     * F000:0100 interrupt handler:
     *   MOV BYTE PTR [0300],5A
     *   JMP FAR F000:0040
     *
     * IRET is intentionally deferred. Gate 9 validates external INT acceptance,
     * the two acknowledge cycles, vector injection, stack saves, IVT lookup,
     * and handler execution as one bounded capability.
     */
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

    /* IVT vector 20h -> F000:0100, stored in RAM at physical 00080h. */
    ram[IVT_ENTRY_ADDR + 0u] = (uint8_t)(ISR_OFFSET & 0xFFu);
    ram[IVT_ENTRY_ADDR + 1u] = (uint8_t)(ISR_OFFSET >> 8);
    ram[IVT_ENTRY_ADDR + 2u] = (uint8_t)(ISR_SEGMENT & 0xFFu);
    ram[IVT_ENTRY_ADDR + 3u] = (uint8_t)(ISR_SEGMENT >> 8);
}

static bool read_memory_for_cycle(const pi86_memory_t *memory,
                                  const v30_bus_cycle_t *cycle,
                                  uint16_t *driven) {
    if (cycle->lanes == V30_BUS_LANES_WORD) {
        return pi86_memory_read16(memory, cycle->address, driven);
    }
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

/*
 * Interrupt acknowledge cycles contain NEC-defined Ti states. Do not assume
 * that the normal two post-control clocks used by memory reads are sufficient.
 * Keep the vector stable (second acknowledge only) until INTAK deasserts.
 */
static bool finish_inta_cycle(v30_bus_t *bus,
                              bool drive_vector,
                              uint8_t vector,
                              uint *steps,
                              uint16_t *last_ad) {
    if (drive_vector) {
        /* NEC specifies an 8-bit vector on AD7..AD0 during acknowledge #2. */
        v30_bus_drive_data(vector, V30_BUS_LANE_LOW);
    } else {
        v30_bus_release_ad();
    }

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

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 9 V30 maskable interrupt / INTA vector test\n");
    printf("Synthetic interrupt source only; no 8259 PIC model yet.\n");
    printf("INT vector=%02Xh, IVT[%05X] -> %04X:%04X, handler marker [0300]=%02X.\n",
           INTERRUPT_VECTOR, IVT_ENTRY_ADDR, ISR_SEGMENT, ISR_OFFSET, ISR_MARKER_VALUE);
    printf("Acceptance requires two INTAK cycles; vector is driven on acknowledge #2 only.\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, PI86_RESET_CLOCKS);

    bool pass = true;
    bool first_read_ok = false;
    bool intr_asserted = false;
    bool intr_deasserted = false;
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

        /* INTAK has priority over normal lane/type decoding; A0 is not an address here. */
        if (cycle.inta_n == 0u) {
            ++inta_count;
            const bool second = inta_count == 2u;
            uint finish_steps = 0u;
            uint16_t last_ad = 0u;

            if (inta_count == 1u) saw_inta1 = true;
            if (inta_count == 2u) saw_inta2 = true;
            if (inta_count > 2u) {
                printf("BUS #%u FAIL: unexpected third INTAK cycle.\n", cycles);
                pass = false;
                break;
            }

            if (!finish_inta_cycle(&bus,
                                   second,
                                   INTERRUPT_VECTOR,
                                   &finish_steps,
                                   &last_ad)) {
                printf("BUS #%u FAIL: INTAK #%u did not deassert within %u steps.\n",
                       cycles, inta_count, MAX_INTA_FINISH_STEPS);
                pass = false;
                break;
            }

            if (second) {
                v30_bus_set_intr(false);
                intr_deasserted = true;
            }

            printf("BUS #%u INTA #%u: idle=%lu A0/BHE#=%u/%u finish_steps=%u%s last_AD=%04X\n",
                   cycles,
                   inta_count,
                   (unsigned long)cycle.idle_steps,
                   cycle.a0, cycle.bhe_n,
                   finish_steps,
                   second ? " vector=20h" : " no-vector",
                   last_ad);

            ++cycles;
            continue;
        }

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t driven = 0u;
            if (!read_memory_for_cycle(&memory, &cycle, &driven)) {
                printf("BUS #%u FAIL: unmapped/invalid memory read 0x%05lX lane=%s.\n",
                       cycles, (unsigned long)cycle.address, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            if (cycles == 0u) {
                first_read_ok = cycle.address == RESET_VECTOR_ADDR &&
                                cycle.lanes == V30_BUS_LANES_WORD;
                if (!first_read_ok) {
                    printf("BUS #0 FAIL: expected reset-vector WORD read at 0x%05X.\n",
                           RESET_VECTOR_ADDR);
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
                if (!intr_asserted && wait_hits >= 2u) {
                    v30_bus_set_intr(true);
                    intr_asserted = true;
                    printf("          >>> INT asserted after wait-loop hit %u <<<\n", wait_hits);
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
                printf("BUS #%u FAIL: unmapped/invalid memory write 0x%05lX lane=%s data=%04X.\n",
                       cycles, (unsigned long)cycle.address, lane_name(cycle.lanes), d0);
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
            printf("BUS #%u FAIL: unsupported non-INTA cycle type=%u address=0x%05lX IO/M=%u DT/R=%u INTA#=%u.\n",
                   cycles,
                   (unsigned)cycle.type,
                   (unsigned long)cycle.address,
                   cycle.iom, cycle.dtr, cycle.inta_n);
            pass = false;
            break;
        }

        ++cycles;
    }

    uint8_t marker = 0u;
    const bool marker_ok = pi86_memory_read8(&memory, ISR_MARKER_ADDR, &marker) &&
                           marker == ISR_MARKER_VALUE;
    const bool stack_saves_ok = stack_seen_7ffa && stack_seen_7ffc && stack_seen_7ffe;

    pass = pass &&
           first_read_ok &&
           intr_asserted && intr_deasserted &&
           saw_inta1 && saw_inta2 && inta_count == 2u &&
           stack_saves_ok &&
           saw_ivt_offset_read && saw_ivt_segment_read &&
           saw_isr_fetch && saw_isr_marker_write && marker_ok &&
           !saw_fail_loop &&
           success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles             = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector WORD read    = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("Wait-loop hits before/after INT = %u\n", wait_hits);
    printf("INT asserted / deasserted       = %s / %s\n",
           intr_asserted ? "YES" : "NO", intr_deasserted ? "YES" : "NO");
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
    printf("GATE 9 INTERRUPT RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_set_intr(false);
    v30_bus_safe_halt(&bus, PI86_RESET_CLOCKS);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
