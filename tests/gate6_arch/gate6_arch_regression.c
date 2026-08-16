#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "memory/memory.h"
#include "v30/v30_bus.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_CYCLES               128u
#define SUCCESS_HITS_REQUIRED          3u

#define ROM_BASE                 0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_BASE                 0x00000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u
#define TEST_RAM_ADDR            0x00200u
#define TEST_RAM_VALUE           0x1234u
#define SUCCESS_LOOP_ADDR        0xF0020u
#define FAIL_LOOP_ADDR           0xF0030u

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];

static void init_test_image(void) {
    for (uint32_t i = 0; i < ROM_SIZE; ++i) rom[i] = 0x90u;
    for (uint32_t i = 0; i < RAM_SIZE; ++i) ram[i] = 0x00u;

    static const uint8_t program[] = {
        0xB8, 0x00, 0x00,             /* MOV AX,0000 */
        0x8E, 0xD8,                   /* MOV DS,AX */
        0xB8, 0x34, 0x12,             /* MOV AX,1234 */
        0xA3, 0x00, 0x02,             /* MOV [0200],AX */
        0xA1, 0x00, 0x02,             /* MOV AX,[0200] */
        0x3D, 0x34, 0x12,             /* CMP AX,1234 */
        0x75, 0x05,                   /* JNE fail */
        0xEA, 0x20, 0x00, 0x00, 0xF0, /* JMP FAR F000:0020 */
        0xEA, 0x30, 0x00, 0x00, 0xF0, /* JMP FAR F000:0030 */
    };

    for (uint32_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

    rom[0x0020] = 0xEBu;
    rom[0x0021] = 0xFEu;
    rom[0x0030] = 0xEBu;
    rom[0x0031] = 0xFEu;

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

    printf("\nGate 6R refactored bus/memory architecture regression test\n");
    printf("Acceptance must reproduce Gate 6: 0x00200 <- 0x1234, CPU readback, SUCCESS F0020.\n");
    printf("Only aligned 16-bit memory read/write is accepted in this regression.\n\n");
    fflush(stdout);

    v30_bus_t bus;
    v30_bus_init(&bus, pio0, STEP_PIO_CLOCK_HZ);
    v30_bus_reset_sequence(&bus, PI86_RESET_CLOCKS);

    bool pass = true;
    bool first_read_ok = false;
    bool saw_test_write = false;
    bool saw_test_read_after_write = false;
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

        if (cycle.lanes != V30_BUS_LANES_WORD ||
            (cycle.type != V30_BUS_CYCLE_MEM_READ &&
             cycle.type != V30_BUS_CYCLE_MEM_WRITE)) {
            printf("BUS #%u FAIL: regression scope violation at 0x%05lX lanes=%u type=%u A0/BHE#=%u/%u IO/M DT/R INTA#=%u %u %u\n",
                   cycles,
                   (unsigned long)cycle.address,
                   (unsigned)cycle.lanes,
                   (unsigned)cycle.type,
                   cycle.a0,
                   cycle.bhe_n,
                   cycle.iom,
                   cycle.dtr,
                   cycle.inta_n);
            pass = false;
            break;
        }

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t word = 0u;
            if (!pi86_memory_read16(&memory, cycle.address, &word)) {
                printf("BUS #%u FAIL: unmapped read at 0x%05lX.\n",
                       cycles, (unsigned long)cycle.address);
                pass = false;
                break;
            }

            if (cycles == 0u) {
                first_read_ok = cycle.address == RESET_VECTOR_ADDR;
                if (!first_read_ok) {
                    printf("BUS #0 FAIL: first read 0x%05lX, expected 0x%05X.\n",
                           (unsigned long)cycle.address, RESET_VECTOR_ADDR);
                    pass = false;
                    break;
                }
            }

            v30_bus_drive_data(word, cycle.lanes);
            uint16_t rb1 = 0u;
            uint16_t rb2 = 0u;
            v30_bus_complete_read(&bus, &rb1, &rb2);

            if (rb1 != word || rb2 != word) {
                printf("BUS #%u FAIL: read pad mismatch at 0x%05lX requested=%04X readback=%04X/%04X\n",
                       cycles, (unsigned long)cycle.address, word, rb1, rb2);
                pass = false;
                break;
            }

            if (cycle.address == TEST_RAM_ADDR && saw_test_write && word == TEST_RAM_VALUE)
                saw_test_read_after_write = true;
            if (cycle.address == SUCCESS_LOOP_ADDR) ++success_hits;
            if (cycle.address == FAIL_LOOP_ADDR) saw_fail_loop = true;

            printf("BUS #%u READ : idle=%lu address=0x%05lX word=0x%04X data=0x%04X/0x%04X%s%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   word, rb1, rb2,
                   cycle.address == TEST_RAM_ADDR ? "  <RAM TEST READ>" : "",
                   cycle.address == SUCCESS_LOOP_ADDR ? "  <SUCCESS>" :
                   (cycle.address == FAIL_LOOP_ADDR ? "  <FAIL>" : ""));
        } else {
            uint16_t d0 = 0u;
            uint16_t d1 = 0u;
            uint16_t d2 = 0u;
            v30_bus_complete_write(&bus, &cycle, &d0, &d1, &d2);

            if (!pi86_memory_write16(&memory, cycle.address, d0)) {
                printf("BUS #%u FAIL: unmapped write at 0x%05lX data=%04X.\n",
                       cycles, (unsigned long)cycle.address, d0);
                pass = false;
                break;
            }

            if (cycle.address == TEST_RAM_ADDR && d0 == TEST_RAM_VALUE)
                saw_test_write = true;

            printf("BUS #%u WRITE: idle=%lu address=0x%05lX data=%04X/%04X/%04X%s\n",
                   cycles,
                   (unsigned long)cycle.idle_steps,
                   (unsigned long)cycle.address,
                   d0, d1, d2,
                   cycle.address == TEST_RAM_ADDR ? "  <RAM TEST WRITE>" : "");
        }

        ++cycles;
    }

    uint16_t final_ram = 0u;
    const bool final_ram_ok = pi86_memory_read16(&memory, TEST_RAM_ADDR, &final_ram) &&
                              final_ram == TEST_RAM_VALUE;

    pass = pass &&
           first_read_ok &&
           saw_test_write &&
           final_ram_ok &&
           saw_test_read_after_write &&
           !saw_fail_loop &&
           success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles           = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector read       = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("Observed RAM write 0200=1234  = %s\n", saw_test_write ? "YES" : "NO");
    printf("RAM backend final [0200]      = 0x%04X\n", final_ram);
    printf("Observed RAM readback=1234    = %s\n", saw_test_read_after_write ? "YES" : "NO");
    printf("Success-loop hits F0020       = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("Fail-loop F0030 observed      = %s\n", saw_fail_loop ? "YES" : "NO");
    printf("GATE 6R ARCH REGRESSION RESULT: %s\n", pass ? "PASS" : "FAIL");

    v30_bus_safe_halt(&bus, PI86_RESET_CLOCKS);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
