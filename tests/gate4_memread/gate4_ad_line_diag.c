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

#define DIAG_PIO_CLOCK_HZ        2000000u
#define DIAG_RESET_CLOCKS             20u
#define DIAG_ALE_SEARCH_CLOCKS        64u
#define DIAG_RESET_VECTOR        0xFFFF0u

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

typedef struct {
    const char *name;
    uint8_t ad_bit;
    uint8_t gpio;
} line_desc_t;

/* AD6/AD8 are included as known-good controls from the v3 matrix. */
static const line_desc_t lines[] = {
    {"AD4", 4u, V30_PIN_AD4},
    {"AD7", 7u, V30_PIN_AD7},
    {"AD6-control", 6u, V30_PIN_AD6},
    {"AD8-control", 8u, V30_PIN_AD8},
};

#define LINE_COUNT (sizeof(lines) / sizeof(lines[0]))

typedef struct {
    bool setup_ok;
    uint32_t address;
    uint32_t t1_sample;
    uint32_t control_sample;
    uint32_t hiz_before;
    uint32_t out_low;
    uint32_t oe_low;
    uint32_t pad_low_0;
    uint32_t pad_low_2;
    uint32_t pad_release_after_low;
    uint32_t out_high;
    uint32_t oe_high;
    uint32_t pad_high_0;
    uint32_t pad_high_2;
    uint32_t pad_high_10;
    uint32_t pad_release_after_high;
} line_result_t;

static line_result_t results[LINE_COUNT];

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
        (float)clock_get_hz(clk_sys) / (float)DIAG_PIO_CLOCK_HZ;
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
    for (uint i = 0; i < DIAG_RESET_CLOCKS; ++i) {
        (void)clock_step(pio, sm);
    }
}

static bool enter_first_read_data_phase(PIO pio,
                                        uint sm,
                                        line_result_t *r) {
    assert_reset(pio, sm);
    drive_cpu_input(V30_PIN_RESET, false);

    bool found = false;
    for (uint step = 0; step < DIAG_ALE_SEARCH_CLOCKS; ++step) {
        const uint32_t sample = clock_step(pio, sm);
        if (sample_bit(sample, V30_PIN_ALE) != 0u &&
            decode_address(sample) == DIAG_RESET_VECTOR) {
            r->t1_sample = sample;
            r->address = DIAG_RESET_VECTOR;
            found = true;
            break;
        }
    }

    if (!found) {
        return false;
    }

    if (sample_bit(r->t1_sample, V30_PIN_AD0) != 0u ||
        sample_bit(r->t1_sample, V30_PIN_BHE) != 0u) {
        return false;
    }

    r->control_sample = clock_step(pio, sm);
    if (sample_bit(r->control_sample, V30_PIN_IOM) != 1u ||
        sample_bit(r->control_sample, V30_PIN_DTR) != 0u ||
        sample_bit(r->control_sample, V30_PIN_INTA) != 1u) {
        return false;
    }

    r->setup_ok = true;
    return true;
}

static void run_line_trial(PIO pio,
                           uint sm,
                           const line_desc_t *line,
                           line_result_t *r) {
    *r = (line_result_t){0};

    if (!enter_first_read_data_phase(pio, sm, r)) {
        assert_reset(pio, sm);
        return;
    }

    const uint32_t mask = 1u << line->gpio;

    /* Start with all AD pins high-Z and record the undriven pad state. */
    release_ad_bus();
    r->hiz_before = sio_hw->gpio_in;

    /* Drive only this one AD line LOW. */
    sio_hw->gpio_clr = mask;
    sio_hw->gpio_oe_set = mask;
    r->out_low = sio_hw->gpio_out;
    r->oe_low = sio_hw->gpio_oe;
    r->pad_low_0 = sio_hw->gpio_in;
    busy_wait_us_32(2u);
    r->pad_low_2 = sio_hw->gpio_in;

    sio_hw->gpio_oe_clr = mask;
    busy_wait_us_32(2u);
    r->pad_release_after_low = sio_hw->gpio_in;

    /* Drive only this one AD line HIGH. */
    sio_hw->gpio_set = mask;
    sio_hw->gpio_oe_set = mask;
    r->out_high = sio_hw->gpio_out;
    r->oe_high = sio_hw->gpio_oe;
    r->pad_high_0 = sio_hw->gpio_in;
    busy_wait_us_32(2u);
    r->pad_high_2 = sio_hw->gpio_in;
    busy_wait_us_32(8u);
    r->pad_high_10 = sio_hw->gpio_in;

    sio_hw->gpio_oe_clr = mask;
    busy_wait_us_32(2u);
    r->pad_release_after_high = sio_hw->gpio_in;

    assert_reset(pio, sm);
}

static void print_line(const line_desc_t *line, const line_result_t *r) {
    const uint gpio = line->gpio;

    printf("\n%s / GPIO%u\n", line->name, gpio);
    printf("  setup/address      = %s / 0x%05lX\n",
           r->setup_ok ? "OK" : "FAIL",
           (unsigned long)r->address);

    if (!r->setup_ok) {
        return;
    }

    printf("  high-Z PAD         = %lu\n",
           (unsigned long)sample_bit(r->hiz_before, gpio));
    printf("  drive LOW:  OUT/OE/PAD0/PAD2 = %lu/%lu/%lu/%lu\n",
           (unsigned long)sample_bit(r->out_low, gpio),
           (unsigned long)sample_bit(r->oe_low, gpio),
           (unsigned long)sample_bit(r->pad_low_0, gpio),
           (unsigned long)sample_bit(r->pad_low_2, gpio));
    printf("  release after LOW: PAD = %lu\n",
           (unsigned long)sample_bit(r->pad_release_after_low, gpio));
    printf("  drive HIGH: OUT/OE/PAD0/PAD2/PAD10 = %lu/%lu/%lu/%lu/%lu\n",
           (unsigned long)sample_bit(r->out_high, gpio),
           (unsigned long)sample_bit(r->oe_high, gpio),
           (unsigned long)sample_bit(r->pad_high_0, gpio),
           (unsigned long)sample_bit(r->pad_high_2, gpio),
           (unsigned long)sample_bit(r->pad_high_10, gpio));
    printf("  release after HIGH: PAD = %lu\n",
           (unsigned long)sample_bit(r->pad_release_after_high, gpio));

    const bool low_ok =
        sample_bit(r->out_low, gpio) == 0u &&
        sample_bit(r->oe_low, gpio) == 1u &&
        sample_bit(r->pad_low_2, gpio) == 0u;
    const bool high_ok =
        sample_bit(r->out_high, gpio) == 1u &&
        sample_bit(r->oe_high, gpio) == 1u &&
        sample_bit(r->pad_high_2, gpio) == 1u &&
        sample_bit(r->pad_high_10, gpio) == 1u;

    printf("  line drive result  = LOW:%s HIGH:%s\n",
           low_ok ? "PASS" : "FAIL",
           high_ok ? "PASS" : "FAIL");
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

    printf("\npi86-rp2350 Gate 4 AD4/AD7 line-drive diagnostic\n");
    printf("Each line is tested during the first aligned memory-read data phase at 0xFFFF0.\n");
    printf("Only one AD GPIO is output-enabled at a time; all other AD pins remain high-Z.\n");
    printf("AD6 and AD8 are included as control lines. No PSRAM is used.\n\n");

    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Diagnostic starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO pio = pio0;
    const uint sm = pio_claim_unused_sm(pio, true);
    const uint offset = pio_add_program(pio, &gate4_step_clk_program);
    init_step_clock(pio, sm, offset);

    for (uint i = 0; i < LINE_COUNT; ++i) {
        run_line_trial(pio, sm, &lines[i], &results[i]);
    }

    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < DIAG_RESET_CLOCKS; ++i) {
        (void)clock_step(pio, sm);
    }
    stop_clock_low(pio, sm);

    printf("\n=== AD line diagnostic results ===\n");
    for (uint i = 0; i < LINE_COUNT; ++i) {
        print_line(&lines[i], &results[i]);
    }

    printf("\nInterpretation guide:\n");
    printf("  OUT=1, OE=1, PAD=0 at both 2 us and 10 us => pad is being held low despite RP2350 drive-high intent.\n");
    printf("  A control line reaching PAD=1 under the same sequence confirms the diagnostic phase/path itself works.\n");
    printf("This diagnostic localizes the fault; it does not by itself declare Gate 4 PASS.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 AD-line heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
