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

#define SCAN_PIO_CLOCK_HZ     2000000u
#define SCAN_RESET_CLOCKS          20u
#define SCAN_MAX_STEP              14u
#define SCAN_DRIVE_US               2u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

typedef struct {
    const char *name;
    uint8_t gpio;
} line_desc_t;

static const line_desc_t lines[] = {
    {"AD4", V30_PIN_AD4},
    {"AD7", V30_PIN_AD7},
    {"AD6-control", V30_PIN_AD6},
    {"AD8-control", V30_PIN_AD8},
};

#define LINE_COUNT (sizeof(lines) / sizeof(lines[0]))
#define STEP_COUNT (SCAN_MAX_STEP + 1u)

typedef struct {
    uint32_t natural_sample;
    uint32_t out_sample;
    uint32_t oe_sample;
    uint32_t pad0_sample;
    uint32_t pad2_sample;
    uint32_t release_sample;
} scan_cell_t;

static scan_cell_t results[LINE_COUNT][STEP_COUNT];

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
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

static void init_step_clock(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_step_clk_program_get_default_config(offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1);

    const float divider =
        (float)clock_get_hz(clk_sys) / (float)SCAN_PIO_CLOCK_HZ;
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
    return sio_hw->gpio_in;
}

static void stop_clock_low(PIO pio, uint sm) {
    pio_sm_set_enabled(pio, sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void assert_reset(PIO pio, uint sm) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < SCAN_RESET_CLOCKS; ++i) {
        (void)clock_step(pio, sm);
    }
}

/*
 * Probe one line at one post-reset clock boundary.
 *
 * Each cell is independent: fresh RESET, release, advance exactly target_step
 * complete host-stepped clocks, briefly request HIGH on only the selected AD
 * line while CLK is stalled LOW, then release and return immediately to RESET.
 * This intentionally avoids carrying any probe disturbance into another cell.
 */
static void probe_cell(PIO pio,
                       uint sm,
                       uint gpio,
                       uint target_step,
                       scan_cell_t *cell) {
    *cell = (scan_cell_t){0};

    assert_reset(pio, sm);
    drive_cpu_input(V30_PIN_RESET, false);

    uint32_t sample = sio_hw->gpio_in;
    for (uint step = 1; step <= target_step; ++step) {
        sample = clock_step(pio, sm);
    }

    release_ad_bus();
    cell->natural_sample = sample;

    const uint32_t mask = 1u << gpio;
    sio_hw->gpio_set = mask;
    sio_hw->gpio_oe_set = mask;

    cell->out_sample = sio_hw->gpio_out;
    cell->oe_sample = sio_hw->gpio_oe;
    cell->pad0_sample = sio_hw->gpio_in;
    busy_wait_us_32(SCAN_DRIVE_US);
    cell->pad2_sample = sio_hw->gpio_in;

    sio_hw->gpio_oe_clr = mask;
    cell->release_sample = sio_hw->gpio_in;

    assert_reset(pio, sm);
}

static void print_line_table(uint line_index) {
    const line_desc_t *line = &lines[line_index];

    printf("\n%s / GPIO%u\n", line->name, line->gpio);
    printf(" STEP ALE IOM DTR BHE AD     A19:16 ADDR  HIZ OUT/OE/P0/P2 REL HIGH\n");
    printf(" ---- --- --- --- --- ------ ------- ----- --- ------------ --- ----\n");

    for (uint step = 0; step < STEP_COUNT; ++step) {
        const scan_cell_t *c = &results[line_index][step];
        const uint32_t s = c->natural_sample;
        const bool high_ok =
            sample_bit(c->out_sample, line->gpio) == 1u &&
            sample_bit(c->oe_sample, line->gpio) == 1u &&
            sample_bit(c->pad2_sample, line->gpio) == 1u;

        const uint32_t a19_16 =
            (sample_bit(s, V30_PIN_A16) << 0) |
            (sample_bit(s, V30_PIN_A17) << 1) |
            (sample_bit(s, V30_PIN_A18) << 2) |
            (sample_bit(s, V30_PIN_A19) << 3);

        printf(" %4u  %lu   %lu   %lu   %lu  %04X     %lX   %05lX   %lu    %lu/%lu/%lu/%lu   %lu  %s\n",
               step,
               (unsigned long)sample_bit(s, V30_PIN_ALE),
               (unsigned long)sample_bit(s, V30_PIN_IOM),
               (unsigned long)sample_bit(s, V30_PIN_DTR),
               (unsigned long)sample_bit(s, V30_PIN_BHE),
               decode_ad(s),
               (unsigned long)a19_16,
               (unsigned long)decode_address(s),
               (unsigned long)sample_bit(s, line->gpio),
               (unsigned long)sample_bit(c->out_sample, line->gpio),
               (unsigned long)sample_bit(c->oe_sample, line->gpio),
               (unsigned long)sample_bit(c->pad0_sample, line->gpio),
               (unsigned long)sample_bit(c->pad2_sample, line->gpio),
               (unsigned long)sample_bit(c->release_sample, line->gpio),
               high_ok ? "PASS" : "FAIL");
    }
}

int main(void) {
    configure_header_high_z();
    drive_cpu_input(V30_PIN_RESET, true);
    drive_cpu_input(V30_PIN_CLK, false);
    drive_cpu_input(V30_PIN_INTR, false);
    release_ad_bus();

    stdio_init_all();
    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }

    printf("\npi86-rp2350 Gate 4 AD phase-scan diagnostic\n");
    printf("RP2350 acts as a low-speed digital bus observer; no external scope is required.\n");
    printf("Each cell uses a fresh RESET, then probes one AD line HIGH at one post-release clock boundary.\n");
    printf("Lines: AD4/GPIO5, AD7/GPIO9, AD6/GPIO11 control, AD8/GPIO10 control.\n");
    printf("Steps: 0 (RESET released, no clock yet) through %u complete clocks.\n", SCAN_MAX_STEP);
    printf("Only one AD GPIO is enabled for %u us per cell; all other AD pins remain high-Z.\n", SCAN_DRIVE_US);
    printf("No PSRAM is used.\n\n");

    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Phase scan starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    for (uint line = 0; line < LINE_COUNT; ++line) {
        for (uint step = 0; step < STEP_COUNT; ++step) {
            probe_cell(pio, sm, lines[line].gpio, step, &results[line][step]);
        }
    }

    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < SCAN_RESET_CLOCKS; ++i) {
        (void)clock_step(pio, sm);
    }
    stop_clock_low(pio, sm);

    printf("\n=== AD phase-scan results ===\n");
    printf("HIGH=PASS means OUT=1, OE=1, and PAD remained HIGH after %u us.\n", SCAN_DRIVE_US);
    printf("A phase-dependent FAIL->PASS transition identifies where the V30 stops opposing that line.\n");

    for (uint line = 0; line < LINE_COUNT; ++line) {
        print_line_table(line);
    }

    printf("\nThis is a localization diagnostic only; it does not declare Gate 4 PASS.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 AD-phase heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
