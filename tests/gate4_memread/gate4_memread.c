#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate4_step_clock.pio.h"

#ifndef GATE4_SMOKE_MODE
#define GATE4_SMOKE_MODE 0
#endif

#define GATE4_CPU_CLOCK_HZ              100000u
#define GATE4_PIO_CLOCK_HZ             2000000u
#define GATE4_RESET_CLOCKS                  20u
#define GATE4_RESET_HALT_CLOCKS              6u
#define GATE4_ALE_SEARCH_CLOCKS              64u
#define GATE4_TRACE_CAPACITY                 32u
#define GATE4_GENERIC_TARGET_READS           16u
#define GATE4_SMOKE_REQUIRED_FFFF0_READS      3u

#define GATE4_RESET_VECTOR              0xFFFF0u
#define GATE4_LOOP_WORD                  0xFEEBu
#define GATE4_FILL_WORD                  0x9090u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];

typedef struct {
    uint32_t t1_sample;
    uint32_t control_sample;
    uint32_t address;
    uint16_t data;
    uint8_t a0;
    uint8_t bhe;
    uint8_t iom;
    uint8_t dtr;
    uint8_t inta;
} gate4_trace_entry_t;

typedef enum {
    GATE4_FAIL_NONE = 0,
    GATE4_FAIL_NO_ALE,
    GATE4_FAIL_UNSUPPORTED_LANE,
    GATE4_FAIL_NOT_MEMORY_READ,
} gate4_fail_t;

static gate4_trace_entry_t trace_entries[GATE4_TRACE_CAPACITY];

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

static void init_data_luts(void) {
    for (uint32_t value = 0; value < 256u; ++value) {
        uint32_t lo_mask = 0;
        uint32_t hi_mask = 0;

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
    uint16_t value = 0;
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

static uint16_t test_rom_word(uint32_t address) {
    if ((address & 0xFFFFFu) == GATE4_RESET_VECTOR) {
        /* EB FE = JMP SHORT -2, little-endian word 0xFEEB. */
        return GATE4_LOOP_WORD;
    }

    /* Deterministic filler for speculative/prefetch reads. */
    return GATE4_FILL_WORD;
}

static void release_ad_bus(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static void drive_ad_word(uint16_t word) {
    const uint32_t encoded =
        data_lo_lut[word & 0xFFu] |
        data_hi_lut[(word >> 8) & 0xFFu];

    /*
     * Prepare the complete scattered GPIO value while the AD bus remains
     * input/high-Z, then atomically enable only the AD0..AD15 output drivers.
     */
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = V30_AD_BUS_MASK;
}

static void init_step_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_step_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);

    const float divider =
        (float)clock_get_hz(clk_sys) / (float)GATE4_PIO_CLOCK_HZ;
    sm_config_set_clkdiv(&c, divider);

    pio_gpio_init(pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(pio, sm, V30_PIN_CLK, 1, true);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static uint32_t clock_step(PIO pio, uint sm) {
    pio_sm_put_blocking(pio, sm, 1u);
    (void)pio_sm_get_blocking(pio, sm);

    /* One coherent snapshot of GPIO0..31 while CLK is stalled LOW. */
    return sio_hw->gpio_in;
}

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);

    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void return_cpu_to_reset(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);

    for (uint i = 0; i < GATE4_RESET_HALT_CLOCKS; ++i) {
        (void)clock_step(pio, sm);
    }

    stop_clock_low(pio, sm);
}

static const char *fail_reason_string(gate4_fail_t reason) {
    switch (reason) {
        case GATE4_FAIL_NONE:
            return "none";
        case GATE4_FAIL_NO_ALE:
            return "no ALE observed while stepping the V30 clock";
        case GATE4_FAIL_UNSUPPORTED_LANE:
            return "cycle is not an aligned 16-bit word access (A0=0, BHE=0)";
        case GATE4_FAIL_NOT_MEMORY_READ:
            return "cycle is not a normal memory read (IO/M=1, DT/R=0, INTA=1)";
        default:
            return "unknown";
    }
}

static void print_banner(void) {
    printf("\npi86-rp2350 Gate 4 aligned-word memory read%s\n",
           GATE4_SMOKE_MODE ? " smoke test" : " test");
    printf("Host: Waveshare RP2350-PiZero\n");
    printf("HAT: original Pi86/Homebrew8088 V20/V30 HAT\n");
    printf("CPU: NEC V30 D70116C-8\n\n");

    printf("Clock pulse: nominal %u Hz, host-stepped with CLK stalled LOW between cycles.\n",
           GATE4_CPU_CLOCK_HZ);
    printf("Initial scope: aligned word memory reads only (A0=0, BHE=0).\n");
    printf("Internal deterministic test ROM:\n");
    printf("  0xFFFF0 -> 0xFEEB  (bytes EB FE = JMP SHORT -2)\n");
    printf("  other aligned reads -> 0x9090 filler\n");
    printf("No PSRAM is required or accessed by this target.\n\n");

    if (GATE4_SMOKE_MODE) {
        printf("PASS condition: service the reset-vector read at least %u times\n",
               GATE4_SMOKE_REQUIRED_FFFF0_READS);
        printf("without seeing an unsupported lane or non-memory-read cycle.\n\n");
    } else {
        printf("PASS condition: service %u aligned word memory reads without error.\n\n",
               GATE4_GENERIC_TARGET_READS);
    }

    fflush(stdout);
}

static void print_trace_entry(uint index, const gate4_trace_entry_t *entry) {
    printf("READ #%u\n", index);
    printf("  T1 raw            = 0x%08lX\n",
           (unsigned long)entry->t1_sample);
    printf("  address           = 0x%05lX\n",
           (unsigned long)entry->address);
    printf("  A0 / BHE          = %u / %u\n",
           entry->a0,
           entry->bhe);
    printf("  control raw       = 0x%08lX\n",
           (unsigned long)entry->control_sample);
    printf("  IO/M / DT/R / INTA= %u / %u / %u\n",
           entry->iom,
           entry->dtr,
           entry->inta);
    printf("  data driven       = 0x%04X\n",
           entry->data);
}

int main(void) {
    configure_header_high_z();
    init_data_luts();

    /* Safe powered-static state before USB enumeration. */
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }

    print_banner();
    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Gate 4 starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO pio = pio0;
    const uint clk_sm = pio_claim_unused_sm(pio, true);
    const uint clk_offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, clk_sm, clk_offset);

    /* RESET must be asserted for at least four clocks. Use a wide margin. */
    for (uint i = 0; i < GATE4_RESET_CLOCKS; ++i) {
        (void)clock_step(pio, clk_sm);
    }

    drive_cpu_input(V30_PIN_RESET, false);

    uint trace_count = 0;
    uint reset_vector_reads = 0;
    gate4_fail_t fail_reason = GATE4_FAIL_NONE;

    const uint target_reads = GATE4_SMOKE_MODE
        ? GATE4_TRACE_CAPACITY
        : GATE4_GENERIC_TARGET_READS;

    while (trace_count < target_reads) {
        bool found_ale = false;
        uint32_t t1_sample = 0;

        for (uint step = 0; step < GATE4_ALE_SEARCH_CLOCKS; ++step) {
            t1_sample = clock_step(pio, clk_sm);
            if (sample_bit(t1_sample, V30_PIN_ALE)) {
                found_ale = true;
                break;
            }
        }

        if (!found_ale) {
            fail_reason = GATE4_FAIL_NO_ALE;
            break;
        }

        gate4_trace_entry_t entry = {0};
        entry.t1_sample = t1_sample;
        entry.address = decode_address(t1_sample);
        entry.a0 = (uint8_t)sample_bit(t1_sample, V30_PIN_AD0);
        entry.bhe = (uint8_t)sample_bit(t1_sample, V30_PIN_BHE);

        if (entry.a0 != 0u || entry.bhe != 0u) {
            fail_reason = GATE4_FAIL_UNSUPPORTED_LANE;
            trace_entries[trace_count] = entry;
            ++trace_count;
            break;
        }

        /* Advance exactly one host-controlled clock into T2. */
        entry.control_sample = clock_step(pio, clk_sm);
        entry.iom = (uint8_t)sample_bit(entry.control_sample, V30_PIN_IOM);
        entry.dtr = (uint8_t)sample_bit(entry.control_sample, V30_PIN_DTR);
        entry.inta = (uint8_t)sample_bit(entry.control_sample, V30_PIN_INTA);

        if (entry.iom != 1u || entry.dtr != 0u || entry.inta != 1u) {
            fail_reason = GATE4_FAIL_NOT_MEMORY_READ;
            trace_entries[trace_count] = entry;
            ++trace_count;
            break;
        }

        entry.data = test_rom_word(entry.address);

        /*
         * CLK is stalled LOW at the end of T2. Prepare the complete word,
         * enable the AD output drivers, then advance through T3 and T4.
         */
        drive_ad_word(entry.data);
        (void)clock_step(pio, clk_sm); /* T3 */
        (void)clock_step(pio, clk_sm); /* T4 */
        release_ad_bus();

        trace_entries[trace_count] = entry;
        ++trace_count;

        if (entry.address == GATE4_RESET_VECTOR) {
            ++reset_vector_reads;
        }

        if (GATE4_SMOKE_MODE &&
            reset_vector_reads >= GATE4_SMOKE_REQUIRED_FFFF0_READS) {
            break;
        }
    }

    return_cpu_to_reset(pio, clk_sm);

    printf("\nCaptured/service trace: %u read cycle%s\n\n",
           trace_count,
           trace_count == 1u ? "" : "s");

    for (uint i = 0; i < trace_count; ++i) {
        print_trace_entry(i, &trace_entries[i]);
        printf("\n");
    }

    bool pass = (fail_reason == GATE4_FAIL_NONE);
    if (GATE4_SMOKE_MODE) {
        pass = pass &&
               (reset_vector_reads >= GATE4_SMOKE_REQUIRED_FFFF0_READS);
    } else {
        pass = pass && (trace_count >= GATE4_GENERIC_TARGET_READS);
    }

    if (pass) {
        if (GATE4_SMOKE_MODE) {
            printf("GATE 4 RESULT: PASS - reset-vector word read repeated %u times.\n",
                   reset_vector_reads);
            printf("The V30 repeatedly consumed the internal-SRAM-backed 0xFEEB loop word.\n");
        } else {
            printf("GATE 4 RESULT: PASS - serviced %u aligned word memory reads.\n",
                   trace_count);
        }
    } else {
        printf("GATE 4 RESULT: FAIL\n");
        printf("  reason             = %s\n", fail_reason_string(fail_reason));
        printf("  serviced reads     = %u\n", trace_count);
        printf("  0xFFFF0 read count = %u\n", reset_vector_reads);
    }

    printf("CPU returned to RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 halted heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
