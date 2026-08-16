#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate4_step_clock.pio.h"

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_CYCLES               128u
#define SUCCESS_HITS_REQUIRED          3u

#define ROM_BASE                 0xF0000u
#define ROM_SIZE                 0x10000u
#define RAM_SIZE                 0x10000u
#define RESET_VECTOR_ADDR        0xFFFF0u
#define TEST_RAM_ADDR            0x00200u
#define TEST_RAM_VALUE           0x1234u
#define SUCCESS_LOOP_ADDR        0xF0020u
#define FAIL_LOOP_ADDR           0xF0030u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];
static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static void configure_header_high_z(void) {
    for (uint gpio = RP2350_PIZERO_HEADER_GPIO_FIRST;
         gpio <= RP2350_PIZERO_HEADER_GPIO_LAST;
         ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void drive_cpu_input(uint gpio, bool level) {
    gpio_init(gpio);
    gpio_disable_pulls(gpio);
    gpio_put(gpio, level);
    gpio_set_dir(gpio, GPIO_OUT);
}

static void release_ad_bus(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static void init_data_luts(void) {
    for (uint32_t value = 0; value < 256u; ++value) {
        uint32_t lo_mask = 0u;
        uint32_t hi_mask = 0u;
        for (uint bit = 0; bit < 8u; ++bit) {
            if ((value >> bit) & 1u) {
                lo_mask |= 1u << ad_gpio[bit];
                hi_mask |= 1u << ad_gpio[bit + 8u];
            }
        }
        data_lo_lut[value] = lo_mask;
        data_hi_lut[value] = hi_mask;
    }
}

static uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0; bit < 16u; ++bit) {
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
    }
    return value;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static void drive_ad_word(uint16_t word) {
    const uint32_t encoded =
        data_lo_lut[word & 0xFFu] |
        data_hi_lut[(word >> 8) & 0xFFu];
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = V30_AD_BUS_MASK;
}

static void init_memory(void) {
    for (uint32_t i = 0; i < ROM_SIZE; ++i) rom[i] = 0x90u;
    for (uint32_t i = 0; i < RAM_SIZE; ++i) ram[i] = 0x00u;

    /*
     * F000:0000
     *   B8 00 00          MOV AX,0000
     *   8E D8             MOV DS,AX
     *   B8 34 12          MOV AX,1234
     *   A3 00 02          MOV [0200],AX
     *   A1 00 02          MOV AX,[0200]
     *   3D 34 12          CMP AX,1234
     *   75 05             JNE fail
     *   EA 20 00 00 F0    JMP FAR F000:0020
     * fail:
     *   EA 30 00 00 F0    JMP FAR F000:0030
     */
    static const uint8_t program[] = {
        0xB8, 0x00, 0x00,
        0x8E, 0xD8,
        0xB8, 0x34, 0x12,
        0xA3, 0x00, 0x02,
        0xA1, 0x00, 0x02,
        0x3D, 0x34, 0x12,
        0x75, 0x05,
        0xEA, 0x20, 0x00, 0x00, 0xF0,
        0xEA, 0x30, 0x00, 0x00, 0xF0,
    };
    for (uint32_t i = 0; i < sizeof(program); ++i) rom[i] = program[i];

    /* Success/fail terminal loops. */
    rom[0x0020] = 0xEBu;
    rom[0x0021] = 0xFEu;
    rom[0x0030] = 0xEBu;
    rom[0x0031] = 0xFEu;

    /* Reset vector: JMP FAR F000:0000. */
    rom[0xFFF0] = 0xEAu;
    rom[0xFFF1] = 0x00u;
    rom[0xFFF2] = 0x00u;
    rom[0xFFF3] = 0x00u;
    rom[0xFFF4] = 0xF0u;
    rom[0xFFF5] = 0x90u;
}

static bool memory_read_word(uint32_t address, uint16_t *word) {
    if ((address & 1u) != 0u) return false;

    if (address <= RAM_SIZE - 2u) {
        *word = (uint16_t)ram[address] | ((uint16_t)ram[address + 1u] << 8);
        return true;
    }

    if (address >= ROM_BASE && address <= 0xFFFFEu) {
        const uint32_t offset = address - ROM_BASE;
        *word = (uint16_t)rom[offset] | ((uint16_t)rom[offset + 1u] << 8);
        return true;
    }

    return false;
}

static bool memory_write_word(uint32_t address, uint16_t word) {
    if ((address & 1u) != 0u || address > RAM_SIZE - 2u) return false;
    ram[address] = (uint8_t)(word & 0xFFu);
    ram[address + 1u] = (uint8_t)(word >> 8);
    return true;
}

static void init_step_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_step_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);
    sm_config_set_clkdiv(
        &c,
        (float)clock_get_hz(clk_sys) / (float)STEP_PIO_CLOCK_HZ);
    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static uint32_t pi86_clk(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 1u);
    (void)pio_sm_get_blocking(pio, sm);
    return sio_hw->gpio_in;
}

static void safe_halt(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) (void)pi86_clk(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

int main(void) {
    configure_header_high_z();
    init_data_luts();
    init_memory();

    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 6 aligned RAM write/readback execution test\n");
    printf("ROM: 0xF0000..0xFFFFF; RAM: 0x00000..0x0FFFF, both RP2350 internal SRAM.\n");
    printf("Program writes 0x%04X to physical 0x%05X, reads it back, compares,\n",
           TEST_RAM_VALUE, TEST_RAM_ADDR);
    printf("then branches to SUCCESS at 0x%05X; FAIL loop is 0x%05X.\n",
           SUCCESS_LOOP_ADDR, FAIL_LOOP_ADDR);
    printf("Scope remains aligned 16-bit memory read/write only.\n\n");
    fflush(stdout);

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) (void)pi86_clk(pio, sm);
    drive_cpu_input(V30_PIN_RESET, false);

    bool pass = true;
    bool first_read_ok = false;
    bool saw_test_write = false;
    bool saw_test_read_after_write = false;
    bool saw_fail_loop = false;
    uint success_hits = 0u;
    uint cycles = 0u;

    while (cycles < MAX_BUS_CYCLES && success_hits < SUCCESS_HITS_REQUIRED) {
        bool saw_ale = false;
        uint32_t t1 = 0u;
        uint idle_steps = 0u;

        for (; idle_steps < PI86_MAX_IDLE_STEPS; ++idle_steps) {
            t1 = pi86_clk(pio, sm);
            if (sample_bit(t1, V30_PIN_ALE)) {
                saw_ale = true;
                break;
            }
        }

        if (!saw_ale) {
            printf("BUS #%u FAIL: ALE timeout.\n", cycles);
            pass = false;
            break;
        }

        const uint32_t address = decode_address(t1);
        const uint32_t a0 = sample_bit(t1, V30_PIN_AD0);
        const uint32_t bhe = sample_bit(t1, V30_PIN_BHE);
        const uint32_t control = pi86_clk(pio, sm);
        const uint32_t iom = sample_bit(control, V30_PIN_IOM);
        const uint32_t dtr = sample_bit(control, V30_PIN_DTR);
        const uint32_t inta = sample_bit(control, V30_PIN_INTA);

        if (a0 != 0u || bhe != 0u || iom != 1u || inta != 1u) {
            printf("BUS #%u FAIL: unsupported lane/control at 0x%05lX: A0/BHE=%lu/%lu IO/M DT/R INTA=%lu %lu %lu\n",
                   cycles,
                   (unsigned long)address,
                   (unsigned long)a0,
                   (unsigned long)bhe,
                   (unsigned long)iom,
                   (unsigned long)dtr,
                   (unsigned long)inta);
            pass = false;
            break;
        }

        if (dtr == 0u) {
            uint16_t word = 0u;
            if (!memory_read_word(address, &word)) {
                printf("BUS #%u FAIL: read outside RAM/ROM at 0x%05lX.\n",
                       cycles, (unsigned long)address);
                pass = false;
                break;
            }

            if (cycles == 0u) {
                first_read_ok = address == RESET_VECTOR_ADDR;
                if (!first_read_ok) {
                    printf("BUS #0 FAIL: first read 0x%05lX, expected 0x%05X.\n",
                           (unsigned long)address, RESET_VECTOR_ADDR);
                    pass = false;
                    break;
                }
            }

            drive_ad_word(word);
            const uint32_t data1 = pi86_clk(pio, sm);
            const uint32_t data2 = pi86_clk(pio, sm);
            const uint16_t rb1 = decode_ad(data1);
            const uint16_t rb2 = decode_ad(data2);
            release_ad_bus();

            if (rb1 != word || rb2 != word) {
                printf("BUS #%u FAIL: read pad mismatch at 0x%05lX requested=0x%04X readback=0x%04X/0x%04X\n",
                       cycles, (unsigned long)address, word, rb1, rb2);
                pass = false;
                break;
            }

            if (address == TEST_RAM_ADDR && saw_test_write && word == TEST_RAM_VALUE) {
                saw_test_read_after_write = true;
            }
            if (address == SUCCESS_LOOP_ADDR) ++success_hits;
            if (address == FAIL_LOOP_ADDR) saw_fail_loop = true;

            printf("BUS #%u READ : idle=%u address=0x%05lX word=0x%04X data=0x%04X/0x%04X%s%s\n",
                   cycles,
                   idle_steps + 1u,
                   (unsigned long)address,
                   word,
                   rb1,
                   rb2,
                   address == TEST_RAM_ADDR ? "  <RAM TEST READ>" : "",
                   address == SUCCESS_LOOP_ADDR ? "  <SUCCESS>" :
                   (address == FAIL_LOOP_ADDR ? "  <FAIL>" : ""));
        } else {
            /* Early pi86 V30 aligned word memory-write case: control 0x07. */
            release_ad_bus();
            const uint16_t write0 = decode_ad(control);
            const uint32_t data1 = pi86_clk(pio, sm);
            const uint32_t data2 = pi86_clk(pio, sm);
            const uint16_t write1 = decode_ad(data1);
            const uint16_t write2 = decode_ad(data2);

            if (!memory_write_word(address, write0)) {
                printf("BUS #%u FAIL: write outside RAM at 0x%05lX data=0x%04X.\n",
                       cycles, (unsigned long)address, write0);
                pass = false;
                break;
            }

            if (address == TEST_RAM_ADDR && write0 == TEST_RAM_VALUE) {
                saw_test_write = true;
            }

            printf("BUS #%u WRITE: idle=%u address=0x%05lX data=%04X/%04X/%04X%s\n",
                   cycles,
                   idle_steps + 1u,
                   (unsigned long)address,
                   write0,
                   write1,
                   write2,
                   address == TEST_RAM_ADDR ? "  <RAM TEST WRITE>" : "");
        }

        ++cycles;
        if (saw_fail_loop) {
            printf("FAIL: CPU entered fail loop at 0x%05X.\n", FAIL_LOOP_ADDR);
            pass = false;
            break;
        }
    }

    safe_halt(pio, sm);

    const uint16_t ram_value =
        (uint16_t)ram[TEST_RAM_ADDR] |
        ((uint16_t)ram[TEST_RAM_ADDR + 1u] << 8);
    const bool final_pass =
        pass && first_read_ok && saw_test_write && saw_test_read_after_write &&
        !saw_fail_loop && ram_value == TEST_RAM_VALUE &&
        success_hits >= SUCCESS_HITS_REQUIRED;

    printf("\nServiced bus cycles           = %u/%u max\n", cycles, MAX_BUS_CYCLES);
    printf("First reset-vector read       = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("Observed RAM write 0200=1234  = %s\n", saw_test_write ? "YES" : "NO");
    printf("RAM backend final [0200]      = 0x%04X\n", ram_value);
    printf("Observed RAM readback=1234    = %s\n",
           saw_test_read_after_write ? "YES" : "NO");
    printf("Success-loop hits F0020       = %u/%u required\n",
           success_hits, SUCCESS_HITS_REQUIRED);
    printf("Fail-loop F0030 observed      = %s\n", saw_fail_loop ? "YES" : "NO");
    printf("GATE 6 RAM WRITE/READ RESULT: %s\n", final_pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
