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

/*
 * Gate 5: minimal executable ROM backed by RP2350 internal memory.
 *
 * Reset-vector code:
 *   FFFF:0000 (physical FFFF0): EA 00 00 00 F0 90
 *       JMP FAR F000:0000
 *
 * Target code:
 *   F000:0000 (physical F0000): 90 90 EB FC
 *       NOP
 *       NOP
 *       JMP SHORT F0000
 *
 * The test intentionally remains within the already-validated Gate 4 scope:
 * aligned 16-bit normal memory reads only. No writes, byte/odd lanes, I/O,
 * interrupts, or external PSRAM are used here.
 */

#define STEP_PIO_CLOCK_HZ       2000000u
#define PI86_RESET_CLOCKS              8u
#define PI86_MAX_IDLE_STEPS           64u
#define MAX_BUS_READS                 64u
#define TARGET_HITS_REQUIRED           3u

#define RESET_VECTOR_ADDR        0xFFFF0u
#define TARGET_LOOP_ADDR         0xF0000u
#define ROM_BASE                 0xF0000u
#define ROM_SIZE                 0x10000u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint8_t rom[ROM_SIZE];
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

static void init_rom(void) {
    for (uint32_t i = 0; i < ROM_SIZE; ++i) {
        rom[i] = 0x90u; /* NOP filler */
    }

    /* F000:0000 -> NOP, NOP, JMP SHORT -4 -> F000:0000. */
    rom[0x0000] = 0x90u;
    rom[0x0001] = 0x90u;
    rom[0x0002] = 0xEBu;
    rom[0x0003] = 0xFCu;

    /* FFFF:0000 / physical FFFF0 -> JMP FAR F000:0000, then NOP filler. */
    rom[0xFFF0] = 0xEAu;
    rom[0xFFF1] = 0x00u;
    rom[0xFFF2] = 0x00u;
    rom[0xFFF3] = 0x00u;
    rom[0xFFF4] = 0xF0u;
    rom[0xFFF5] = 0x90u;
}

static bool rom_read_word(uint32_t address, uint16_t *word) {
    if ((address & 1u) != 0u || address < ROM_BASE || address > 0xFFFFEu) {
        return false;
    }
    const uint32_t offset = address - ROM_BASE;
    *word = (uint16_t)rom[offset] | ((uint16_t)rom[offset + 1u] << 8);
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
    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

int main(void) {
    configure_header_high_z();
    init_data_luts();
    init_rom();

    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 5 minimal executable ROM test\n");
    printf("ROM backend: RP2350 internal SRAM, physical 0x%05X..0xFFFFF.\n", ROM_BASE);
    printf("Reset vector: EA 00 00 00 F0 -> JMP FAR F000:0000.\n");
    printf("Target loop: 90 90 EB FC -> NOP, NOP, JMP SHORT -4.\n");
    printf("PASS: first read is 0xFFFF0 and target 0xF0000 is observed >= %u times\n",
           TARGET_HITS_REQUIRED);
    printf("within %u aligned memory reads, with correct pad readback every cycle.\n\n",
           MAX_BUS_READS);
    fflush(stdout);

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    for (uint i = 0; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }
    drive_cpu_input(V30_PIN_RESET, false);

    bool pass = true;
    bool first_read_ok = false;
    uint target_hits = 0u;
    uint reads = 0u;

    while (reads < MAX_BUS_READS && target_hits < TARGET_HITS_REQUIRED) {
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
            printf("BUS #%u FAIL: ALE not observed within %u CLK() calls.\n",
                   reads, PI86_MAX_IDLE_STEPS);
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

        if (a0 != 0u || bhe != 0u || iom != 1u || dtr != 0u || inta != 1u) {
            printf("BUS #%u FAIL: unsupported lane/control cycle.\n", reads);
            printf("  address=0x%05lX A0/BHE=%lu/%lu IO/M DT/R INTA=%lu %lu %lu\n",
                   (unsigned long)address,
                   (unsigned long)a0,
                   (unsigned long)bhe,
                   (unsigned long)iom,
                   (unsigned long)dtr,
                   (unsigned long)inta);
            pass = false;
            break;
        }

        uint16_t word = 0u;
        if (!rom_read_word(address, &word)) {
            printf("BUS #%u FAIL: aligned memory read outside minimal ROM: 0x%05lX.\n",
                   reads, (unsigned long)address);
            pass = false;
            break;
        }

        if (reads == 0u) {
            first_read_ok = address == RESET_VECTOR_ADDR;
            if (!first_read_ok) {
                printf("BUS #0 FAIL: first read is 0x%05lX, expected 0x%05X.\n",
                       (unsigned long)address, RESET_VECTOR_ADDR);
                pass = false;
                break;
            }
        }

        drive_ad_word(word);
        const uint32_t data1 = pi86_clk(pio, sm);
        const uint32_t data2 = pi86_clk(pio, sm);
        const uint16_t readback1 = decode_ad(data1);
        const uint16_t readback2 = decode_ad(data2);
        release_ad_bus();

        if (readback1 != word || readback2 != word) {
            printf("BUS #%u FAIL: data pad mismatch at 0x%05lX. requested=0x%04X readback=0x%04X/0x%04X\n",
                   reads,
                   (unsigned long)address,
                   word,
                   readback1,
                   readback2);
            pass = false;
            break;
        }

        const bool target = address == TARGET_LOOP_ADDR;
        if (target) ++target_hits;

        printf("BUS #%u PASS: idle=%u address=0x%05lX word=0x%04X data=0x%04X/0x%04X%s\n",
               reads,
               idle_steps + 1u,
               (unsigned long)address,
               word,
               readback1,
               readback2,
               target ? "  <TARGET F0000>" : "");
        ++reads;
    }

    safe_halt(pio, sm);

    const bool final_pass = pass && first_read_ok && target_hits >= TARGET_HITS_REQUIRED;
    printf("\nServiced aligned memory reads = %u/%u max\n", reads, MAX_BUS_READS);
    printf("First reset-vector read       = %s\n", first_read_ok ? "PASS" : "FAIL");
    printf("Target F0000 hits             = %u/%u required\n",
           target_hits, TARGET_HITS_REQUIRED);
    printf("GATE 5 MINIMAL ROM RESULT: %s\n", final_pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
