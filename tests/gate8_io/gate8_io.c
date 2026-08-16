#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "io/io.h"
#include "memory/memory.h"
#include "v30/v30_bus.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_CYCLES               160u
#define SUCCESS_HITS_REQUIRED          3u

#define ROM_BASE                 0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u

#define IO_BASE                     0x80u
#define IO_SIZE                        2u
#define EVEN_PORT                    0x80u
#define ODD_PORT                     0x81u
#define EVEN_VALUE                   0x5Au
#define ODD_VALUE                    0xA5u

#define SUCCESS_LOOP_ADDR        0xF0040u
#define FAIL_LOOP_ADDR           0xF0050u

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];
static uint8_t io_storage[IO_SIZE];

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
    for (uint32_t i = 0; i < IO_SIZE; ++i) io_storage[i] = 0x00u;

    /*
     * F000:0000
     *   MOV AL,5A
     *   OUT 80h,AL
     *   MOV AL,00
     *   IN  AL,80h
     *   CMP AL,5A
     *   JNE fail
     *
     *   MOV AL,A5
     *   OUT 81h,AL
     *   MOV AL,00
     *   IN  AL,81h
     *   CMP AL,A5
     *   JNE fail
     *
     *   JMP FAR F000:0040
     * fail:
     *   JMP FAR F000:0050
     */
    static const uint8_t program[] = {
        0xB0, 0x5A,
        0xE6, 0x80,
        0xB0, 0x00,
        0xE4, 0x80,
        0x3C, 0x5A,
        0x75, 0x11,
        0xB0, 0xA5,
        0xE6, 0x81,
        0xB0, 0x00,
        0xE4, 0x81,
        0x3C, 0xA5,
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

    pi86_io_t io;
    pi86_io_init(&io, io_storage, IO_BASE, IO_SIZE);

    v30_bus_hold_reset(true);
    v30_bus_set_intr(false);
    v30_bus_release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 8 V30 I/O-space IN/OUT execution test\n");
    printf("Synthetic backend only: ports 80h and 81h. No PIC/PIT/device semantics yet.\n");
    printf("Program verifies OUT/IN/CMP on even LOW-lane port 80h and odd HIGH-lane port 81h.\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, PI86_RESET_CLOCKS);

    bool pass = true;
    bool first_read_ok = false;
    bool saw_even_out = false;
    bool saw_even_in = false;
    bool saw_odd_out = false;
    bool saw_odd_in = false;
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

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t driven = 0u;

            if (cycle.lanes == V30_BUS_LANES_WORD) {
                if (!pi86_memory_read16(&memory, cycle.address, &driven)) {
                    printf("BUS #%u FAIL: unmapped WORD memory read at 0x%05lX.\n",
                           cycles, (unsigned long)cycle.address);
                    pass = false;
                    break;
                }
            } else if (cycle.lanes == V30_BUS_LANE_LOW) {
                uint8_t value = 0u;
                if (!pi86_memory_read8(&memory, cycle.address, &value)) {
                    printf("BUS #%u FAIL: unmapped LOW memory read at 0x%05lX.\n",
                           cycles, (unsigned long)cycle.address);
                    pass = false;
                    break;
                }
                driven = value;
            } else if (cycle.lanes == V30_BUS_LANE_HIGH) {
                uint8_t value = 0u;
                if (!pi86_memory_read8(&memory, cycle.address, &value)) {
                    printf("BUS #%u FAIL: unmapped HIGH memory read at 0x%05lX.\n",
                           cycles, (unsigned long)cycle.address);
                    pass = false;
                    break;
                }
                driven = (uint16_t)value << 8;
            } else {
                printf("BUS #%u FAIL: invalid memory-read lane.\n", cycles);
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
                printf("BUS #%u FAIL: memory readback mismatch.\n", cycles);
                pass = false;
                break;
            }

            if (cycle.address == SUCCESS_LOOP_ADDR) ++success_hits;
            if (cycle.address == FAIL_LOOP_ADDR) saw_fail_loop = true;

            printf("BUS #%u MEM RD: idle=%lu address=0x%05lX lane=%-4s A0/BHE#=%u/%u data=%04X/%04X/%04X%s%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   lane_name(cycle.lanes),
                   cycle.a0, cycle.bhe_n,
                   driven, rb1, rb2,
                   cycle.address == SUCCESS_LOOP_ADDR ? "  <SUCCESS>" : "",
                   cycle.address == FAIL_LOOP_ADDR ? "  <FAIL>" : "");
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
                printf("BUS #%u FAIL: Gate 8 supports byte I/O writes only; port=%04X lane=%s.\n",
                       cycles, port, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            if (!pi86_io_write8(&io, port, value)) {
                printf("BUS #%u FAIL: unmapped I/O write port=%04X value=%02X.\n",
                       cycles, port, value);
                pass = false;
                break;
            }

            if (port == EVEN_PORT && cycle.lanes == V30_BUS_LANE_LOW && value == EVEN_VALUE)
                saw_even_out = true;
            if (port == ODD_PORT && cycle.lanes == V30_BUS_LANE_HIGH && value == ODD_VALUE)
                saw_odd_out = true;

            printf("BUS #%u IO  WR: idle=%lu port=%04X lane=%-4s A0/BHE#=%u/%u IO/M=%u data=%04X/%04X/%04X stored=%02X\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   port,
                   lane_name(cycle.lanes),
                   cycle.a0, cycle.bhe_n, cycle.iom,
                   d0, d1, d2, value);
        } else if (cycle.type == V30_BUS_CYCLE_IO_READ) {
            const uint16_t port = (uint16_t)(cycle.address & 0xFFFFu);
            uint8_t value = 0u;
            if (!pi86_io_read8(&io, port, &value)) {
                printf("BUS #%u FAIL: unmapped I/O read port=%04X.\n", cycles, port);
                pass = false;
                break;
            }

            uint16_t driven = 0u;
            if (cycle.lanes == V30_BUS_LANE_LOW) {
                driven = value;
            } else if (cycle.lanes == V30_BUS_LANE_HIGH) {
                driven = (uint16_t)value << 8;
            } else {
                printf("BUS #%u FAIL: Gate 8 supports byte I/O reads only; port=%04X lane=%s.\n",
                       cycles, port, lane_name(cycle.lanes));
                pass = false;
                break;
            }

            v30_bus_drive_data(driven, cycle.lanes);
            uint16_t rb1 = 0u;
            uint16_t rb2 = 0u;
            v30_bus_complete_read(&bus, &rb1, &rb2);

            if (!selected_readback_matches(cycle.lanes, driven, rb1) ||
                !selected_readback_matches(cycle.lanes, driven, rb2)) {
                printf("BUS #%u FAIL: I/O read pad mismatch port=%04X driven=%04X rb=%04X/%04X.\n",
                       cycles, port, driven, rb1, rb2);
                pass = false;
                break;
            }

            if (port == EVEN_PORT && cycle.lanes == V30_BUS_LANE_LOW && value == EVEN_VALUE)
                saw_even_in = true;
            if (port == ODD_PORT && cycle.lanes == V30_BUS_LANE_HIGH && value == ODD_VALUE)
                saw_odd_in = true;

            printf("BUS #%u IO  RD: idle=%lu port=%04X lane=%-4s A0/BHE#=%u/%u IO/M=%u value=%02X data=%04X/%04X/%04X\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   port,
                   lane_name(cycle.lanes),
                   cycle.a0, cycle.bhe_n, cycle.iom,
                   value, driven, rb1, rb2);
        } else {
            printf("BUS #%u FAIL: unsupported cycle type=%u address=0x%05lX IO/M=%u DT/R=%u INTA#=%u.\n",
                   cycles,
                   (unsigned)cycle.type,
                   (unsigned long)cycle.address,
                   cycle.iom, cycle.dtr, cycle.inta_n);
            pass = false;
            break;
        }

        ++cycles;
    }

    uint8_t final_even = 0u;
    uint8_t final_odd = 0u;
    const bool backend_ok =
        pi86_io_read8(&io, EVEN_PORT, &final_even) &&
        pi86_io_read8(&io, ODD_PORT, &final_odd) &&
        final_even == EVEN_VALUE &&
        final_odd == ODD_VALUE;

    pass = pass &&
           first_read_ok &&
           saw_even_out && saw_even_in &&
           saw_odd_out && saw_odd_in &&
           backend_ok &&
           !saw_fail_loop &&
           success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles             = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector WORD read    = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("Even port 80h LOW OUT/IN        = %s / %s\n",
           saw_even_out ? "YES" : "NO", saw_even_in ? "YES" : "NO");
    printf("Odd port 81h HIGH OUT/IN        = %s / %s\n",
           saw_odd_out ? "YES" : "NO", saw_odd_in ? "YES" : "NO");
    printf("I/O backend final [80h,81h]     = %02X %02X\n", final_even, final_odd);
    printf("Expected                        = %02X %02X\n", EVEN_VALUE, ODD_VALUE);
    printf("Success-loop hits F0040         = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("Fail-loop F0050 observed        = %s\n", saw_fail_loop ? "YES" : "NO");
    printf("GATE 8 I/O RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_safe_halt(&bus, PI86_RESET_CLOCKS);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
