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

#define STEP_PIO_CLOCK_HZ 2000000u
#define PI86_RESET_CLOCKS 8u
#define PI86_MAX_IDLE_STEPS 64u
#define MAX_BUS_READS 64u
#define RESET_VECTOR_HITS_REQUIRED 3u
#define RESET_VECTOR 0xFFFF0u
#define LOOP_WORD 0xFEEBu
#define FILL_WORD 0x9090u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

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
    for (uint32_t value = 0u; value < 256u; ++value) {
        uint32_t lo_mask = 0u;
        uint32_t hi_mask = 0u;
        for (uint bit = 0u; bit < 8u; ++bit) {
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
    for (uint bit = 0u; bit < 16u; ++bit) {
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

static uint16_t rom_word(uint32_t address) {
    return address == RESET_VECTOR ? LOOP_WORD : FILL_WORD;
}

static void drive_ad_word(uint16_t word) {
    const uint32_t encoded =
        data_lo_lut[word & 0xFFu] |
        data_hi_lut[(word >> 8) & 0xFFu];
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = V30_AD_BUS_MASK;
}

static void init_step_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_step_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);
    sm_config_set_clkdiv(&c,
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
    for (uint i = 0u; i < PI86_RESET_CLOCKS; ++i) {
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
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nGate 4 pi86 prefetch-aware reset-vector loop smoke test\n");
    printf("RESET=1 for %u CLK() calls, then pi86-style stepped bus service.\n",
           PI86_RESET_CLOCKS);
    printf("ROM: 0x%05X -> 0x%04X (EB FE = JMP SHORT -2); other aligned reads -> 0x%04X.\n",
           RESET_VECTOR, LOOP_WORD, FILL_WORD);
    printf("PASS: observe 0x%05X at least %u times within %u valid aligned memory reads,\n",
           RESET_VECTOR, RESET_VECTOR_HITS_REQUIRED, MAX_BUS_READS);
    printf("with correct data pad readback on every serviced cycle.\n\n");
    fflush(stdout);

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    for (uint i = 0u; i < PI86_RESET_CLOCKS; ++i) {
        (void)pi86_clk(pio, sm);
    }
    drive_cpu_input(V30_PIN_RESET, false);

    bool pass = true;
    uint bus_reads = 0u;
    uint reset_vector_hits = 0u;

    while (bus_reads < MAX_BUS_READS &&
           reset_vector_hits < RESET_VECTOR_HITS_REQUIRED) {
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
                   bus_reads, PI86_MAX_IDLE_STEPS);
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
            printf("BUS #%u FAIL: unsupported/non-memory-read cycle.\n", bus_reads);
            printf("  idle CLK() calls = %u\n", idle_steps + 1u);
            printf("  address          = 0x%05lX\n", (unsigned long)address);
            printf("  A0/BHE           = %lu/%lu\n",
                   (unsigned long)a0, (unsigned long)bhe);
            printf("  IO/M DT/R INTA   = %lu %lu %lu\n",
                   (unsigned long)iom,
                   (unsigned long)dtr,
                   (unsigned long)inta);
            pass = false;
            break;
        }

        const uint16_t word = rom_word(address);
        drive_ad_word(word);
        const uint32_t data_clk1 = pi86_clk(pio, sm);
        const uint32_t data_clk2 = pi86_clk(pio, sm);
        const uint16_t readback1 = decode_ad(data_clk1);
        const uint16_t readback2 = decode_ad(data_clk2);
        release_ad_bus();

        if (readback1 != word || readback2 != word) {
            printf("BUS #%u FAIL: data readback mismatch at 0x%05lX.\n",
                   bus_reads, (unsigned long)address);
            printf("  requested=0x%04X CLK#1=0x%04X CLK#2=0x%04X\n",
                   word, readback1, readback2);
            pass = false;
            break;
        }

        if (address == RESET_VECTOR) {
            ++reset_vector_hits;
        }

        printf("BUS #%u PASS: idle=%u address=0x%05lX data=0x%04X/0x%04X%s\n",
               bus_reads,
               idle_steps + 1u,
               (unsigned long)address,
               readback1,
               readback2,
               address == RESET_VECTOR ? "  <RESET VECTOR>" : "");
        ++bus_reads;
    }

    safe_halt(pio, sm);

    const bool final_pass =
        pass && reset_vector_hits >= RESET_VECTOR_HITS_REQUIRED;

    printf("\nServiced aligned memory reads = %u/%u max\n",
           bus_reads, MAX_BUS_READS);
    printf("Reset-vector hits            = %u/%u required\n",
           reset_vector_hits, RESET_VECTOR_HITS_REQUIRED);
    printf("GATE 4 PI86 LOOP SMOKE RESULT: %s\n",
           final_pass ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) sleep_ms(1000);
}
