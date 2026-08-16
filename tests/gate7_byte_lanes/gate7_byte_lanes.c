#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "memory/memory.h"
#include "v30/v30_bus.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_CYCLES               192u
#define SUCCESS_HITS_REQUIRED          3u

#define ROM_BASE                 0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u

#define EVEN_BYTE_ADDR           0x00200u
#define ODD_BYTE_ADDR            0x00201u
#define ODD_WORD_NEXT_ADDR       0x00202u
#define EVEN_BYTE_VALUE              0x5Au
#define ODD_BYTE_VALUE               0xA5u
#define ODD_WORD_VALUE             0xBEEFu

#define SUCCESS_LOOP_ADDR        0xF0040u
#define FAIL_LOOP_ADDR           0xF0050u

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];

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
     *   MOV AX,0000
     *   MOV DS,AX
     *
     *   MOV BYTE PTR [0200],5A
     *   MOV AL,[0200]
     *   CMP AL,5A
     *   JNE fail
     *
     *   MOV BYTE PTR [0201],A5
     *   MOV AL,[0201]
     *   CMP AL,A5
     *   JNE fail
     *
     *   MOV AX,BEEF
     *   MOV [0201],AX
     *   MOV AX,[0201]
     *   CMP AX,BEEF
     *   JNE fail
     *
     *   JMP FAR F000:0040
     * fail:
     *   JMP FAR F000:0050
     *
     * The odd word at 0201 must be split by the V30 into two byte-lane
     * transactions: HIGH lane at 0201 for EF, then LOW lane at 0202 for BE.
     */
    static const uint8_t program[] = {
        0xB8, 0x00, 0x00,
        0x8E, 0xD8,
        0xC6, 0x06, 0x00, 0x02, 0x5A,
        0xA0, 0x00, 0x02,
        0x3C, 0x5A,
        0x75, 0x1F,
        0xC6, 0x06, 0x01, 0x02, 0xA5,
        0xA0, 0x01, 0x02,
        0x3C, 0xA5,
        0x75, 0x13,
        0xB8, 0xEF, 0xBE,
        0xA3, 0x01, 0x02,
        0xA1, 0x01, 0x02,
        0x3D, 0xEF, 0xBE,
        0x75, 0x05,
        0xEA, 0x40, 0x00, 0x00, 0xF0,
        0xEA, 0x50, 0x00, 0x00, 0xF0,
    };

    for (uint32_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

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

    printf("\nGate 7 V30 byte-lane / odd-word memory execution test\n");
    printf("Backend: byte-addressed RP2350 SRAM RAM/ROM using reusable v30_bus + memory layers.\n");
    printf("Program verifies: even byte @0200, odd byte @0201, odd word BEEF @0201.\n");
    printf("Expected odd-word split: HIGH lane 0201=EF, LOW lane 0202=BE.\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, PI86_RESET_CLOCKS);

    bool pass = true;
    bool first_read_ok = false;
    bool saw_even_byte_write = false;
    bool saw_even_byte_read = false;
    bool saw_odd_byte_write = false;
    bool saw_odd_byte_read = false;
    bool saw_odd_word_write_first = false;
    bool saw_odd_word_write_second = false;
    bool saw_odd_word_read_first = false;
    bool saw_odd_word_read_second = false;
    bool saw_fail_loop = false;
    uint success_hits = 0u;
    uint cycles = 0u;

    while (cycles < MAX_BUS_CYCLES && success_hits < SUCCESS_HITS_REQUIRED) {
        v30_bus_cycle_t cycle;
        if (!v30_bus_wait_cycle(&bus, PI86_MAX_IDLE_STEPS, &cycle)) {
            printf("BUS #%u FAIL: ALE timeout.\n", cycles);
            pass = false;
            break;
        }

        if (cycle.type != V30_BUS_CYCLE_MEM_READ &&
            cycle.type != V30_BUS_CYCLE_MEM_WRITE) {
            printf("BUS #%u FAIL: unsupported cycle type=%u at 0x%05lX.\n",
                   cycles, (unsigned)cycle.type, (unsigned long)cycle.address);
            pass = false;
            break;
        }

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t driven = 0u;

            if (cycle.lanes == V30_BUS_LANES_WORD) {
                if (!pi86_memory_read16(&memory, cycle.address, &driven)) {
                    printf("BUS #%u FAIL: unmapped WORD read at 0x%05lX.\n",
                           cycles, (unsigned long)cycle.address);
                    pass = false;
                    break;
                }
            } else if (cycle.lanes == V30_BUS_LANE_LOW) {
                uint8_t value = 0u;
                if (!pi86_memory_read8(&memory, cycle.address, &value)) {
                    printf("BUS #%u FAIL: unmapped LOW read at 0x%05lX.\n",
                           cycles, (unsigned long)cycle.address);
                    pass = false;
                    break;
                }
                driven = value;
            } else if (cycle.lanes == V30_BUS_LANE_HIGH) {
                uint8_t value = 0u;
                if (!pi86_memory_read8(&memory, cycle.address, &value)) {
                    printf("BUS #%u FAIL: unmapped HIGH read at 0x%05lX.\n",
                           cycles, (unsigned long)cycle.address);
                    pass = false;
                    break;
                }
                driven = (uint16_t)value << 8;
            } else {
                printf("BUS #%u FAIL: invalid read lane at 0x%05lX A0/BHE#=%u/%u.\n",
                       cycles, (unsigned long)cycle.address, cycle.a0, cycle.bhe_n);
                pass = false;
                break;
            }

            if (cycles == 0u) {
                first_read_ok = cycle.address == RESET_VECTOR_ADDR &&
                                cycle.lanes == V30_BUS_LANES_WORD;
                if (!first_read_ok) {
                    printf("BUS #0 FAIL: expected WORD reset-vector read at 0x%05X.\n",
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
                printf("BUS #%u FAIL: %s read pad mismatch address=0x%05lX driven=%04X readback=%04X/%04X\n",
                       cycles, lane_name(cycle.lanes), (unsigned long)cycle.address,
                       driven, rb1, rb2);
                pass = false;
                break;
            }

            if (cycle.address == EVEN_BYTE_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)driven == EVEN_BYTE_VALUE) {
                saw_even_byte_read = true;
            }
            if (cycle.address == ODD_BYTE_ADDR &&
                cycle.lanes == V30_BUS_LANE_HIGH) {
                const uint8_t value = (uint8_t)(driven >> 8);
                if (value == ODD_BYTE_VALUE) saw_odd_byte_read = true;
                if (value == (uint8_t)(ODD_WORD_VALUE & 0xFFu))
                    saw_odd_word_read_first = true;
            }
            if (cycle.address == ODD_WORD_NEXT_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)driven == (uint8_t)(ODD_WORD_VALUE >> 8)) {
                saw_odd_word_read_second = true;
            }

            if (cycle.address == SUCCESS_LOOP_ADDR) ++success_hits;
            if (cycle.address == FAIL_LOOP_ADDR) saw_fail_loop = true;

            printf("BUS #%u READ : idle=%lu address=0x%05lX lane=%-4s A0/BHE#=%u/%u data=%04X/%04X/%04X%s%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   cycle.a0, cycle.bhe_n,
                   driven, rb1, rb2,
                   cycle.address == SUCCESS_LOOP_ADDR ? "  <SUCCESS>" : "",
                   cycle.address == FAIL_LOOP_ADDR ? "  <FAIL>" : "");
        } else {
            uint16_t d0 = 0u;
            uint16_t d1 = 0u;
            uint16_t d2 = 0u;
            v30_bus_complete_write(&bus, &cycle, &d0, &d1, &d2);

            if (cycle.lanes == V30_BUS_LANES_WORD) {
                if (!pi86_memory_write16(&memory, cycle.address, d0)) {
                    printf("BUS #%u FAIL: unmapped WORD write at 0x%05lX data=%04X.\n",
                           cycles, (unsigned long)cycle.address, d0);
                    pass = false;
                    break;
                }
            } else if (cycle.lanes == V30_BUS_LANE_LOW) {
                const uint8_t value = (uint8_t)d0;
                if (!pi86_memory_write8(&memory, cycle.address, value)) {
                    printf("BUS #%u FAIL: unmapped LOW write at 0x%05lX data=%02X.\n",
                           cycles, (unsigned long)cycle.address, value);
                    pass = false;
                    break;
                }
            } else if (cycle.lanes == V30_BUS_LANE_HIGH) {
                const uint8_t value = (uint8_t)(d0 >> 8);
                if (!pi86_memory_write8(&memory, cycle.address, value)) {
                    printf("BUS #%u FAIL: unmapped HIGH write at 0x%05lX data=%02X.\n",
                           cycles, (unsigned long)cycle.address, value);
                    pass = false;
                    break;
                }
            } else {
                printf("BUS #%u FAIL: invalid write lane at 0x%05lX A0/BHE#=%u/%u.\n",
                       cycles, (unsigned long)cycle.address, cycle.a0, cycle.bhe_n);
                pass = false;
                break;
            }

            if (cycle.address == EVEN_BYTE_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)d0 == EVEN_BYTE_VALUE) {
                saw_even_byte_write = true;
            }
            if (cycle.address == ODD_BYTE_ADDR &&
                cycle.lanes == V30_BUS_LANE_HIGH) {
                const uint8_t value = (uint8_t)(d0 >> 8);
                if (value == ODD_BYTE_VALUE) saw_odd_byte_write = true;
                if (value == (uint8_t)(ODD_WORD_VALUE & 0xFFu))
                    saw_odd_word_write_first = true;
            }
            if (cycle.address == ODD_WORD_NEXT_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)d0 == (uint8_t)(ODD_WORD_VALUE >> 8)) {
                saw_odd_word_write_second = true;
            }

            printf("BUS #%u WRITE: idle=%lu address=0x%05lX lane=%-4s A0/BHE#=%u/%u data=%04X/%04X/%04X\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   cycle.a0, cycle.bhe_n,
                   d0, d1, d2);
        }

        ++cycles;
    }

    uint8_t final_200 = 0u;
    uint8_t final_201 = 0u;
    uint8_t final_202 = 0u;
    const bool final_memory_ok =
        pi86_memory_read8(&memory, EVEN_BYTE_ADDR, &final_200) &&
        pi86_memory_read8(&memory, ODD_BYTE_ADDR, &final_201) &&
        pi86_memory_read8(&memory, ODD_WORD_NEXT_ADDR, &final_202) &&
        final_200 == EVEN_BYTE_VALUE &&
        final_201 == (uint8_t)(ODD_WORD_VALUE & 0xFFu) &&
        final_202 == (uint8_t)(ODD_WORD_VALUE >> 8);

    pass = pass &&
           first_read_ok &&
           saw_even_byte_write && saw_even_byte_read &&
           saw_odd_byte_write && saw_odd_byte_read &&
           saw_odd_word_write_first && saw_odd_word_write_second &&
           saw_odd_word_read_first && saw_odd_word_read_second &&
           final_memory_ok &&
           !saw_fail_loop &&
           success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles            = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector WORD read   = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("Even byte 0200 LOW write/read  = %s / %s\n",
           saw_even_byte_write ? "YES" : "NO", saw_even_byte_read ? "YES" : "NO");
    printf("Odd byte 0201 HIGH write/read  = %s / %s\n",
           saw_odd_byte_write ? "YES" : "NO", saw_odd_byte_read ? "YES" : "NO");
    printf("Odd word write split 0201/0202 = %s / %s\n",
           saw_odd_word_write_first ? "YES" : "NO",
           saw_odd_word_write_second ? "YES" : "NO");
    printf("Odd word read split 0201/0202  = %s / %s\n",
           saw_odd_word_read_first ? "YES" : "NO",
           saw_odd_word_read_second ? "YES" : "NO");
    printf("RAM final [0200..0202]         = %02X %02X %02X\n",
           final_200, final_201, final_202);
    printf("Expected                       = %02X %02X %02X\n",
           EVEN_BYTE_VALUE,
           (uint8_t)(ODD_WORD_VALUE & 0xFFu),
           (uint8_t)(ODD_WORD_VALUE >> 8));
    printf("Success-loop hits F0040        = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("Fail-loop F0050 observed       = %s\n", saw_fail_loop ? "YES" : "NO");
    printf("GATE 7 BYTE-LANE RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_safe_halt(&bus, PI86_RESET_CLOCKS);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
