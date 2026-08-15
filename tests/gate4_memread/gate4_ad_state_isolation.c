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
#define DIAG_SETTLE_US                 2u

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

typedef enum {
    STATE_RESET20 = 0,
    STATE_RESET40,
    STATE_RELEASE0,
    STATE_READ_DATA,
    STATE_COUNT,
} diag_state_t;

static const char *state_name(diag_state_t state) {
    switch (state) {
        case STATE_RESET20: return "RESET-HIGH after 20 clocks";
        case STATE_RESET40: return "RESET-HIGH after 40 clocks";
        case STATE_RELEASE0: return "RESET-LOW before first post-release clock";
        case STATE_READ_DATA: return "first 0xFFFF0 memory-read data phase";
        default: return "UNKNOWN";
    }
}

typedef struct {
    bool state_ok;
    uint32_t address;
    uint32_t state_sample;
    uint32_t hiz_sample;
    uint32_t out_high;
    uint32_t oe_high;
    uint32_t pad_high_0;
    uint32_t pad_high_2;
    uint32_t release_sample;
} trial_result_t;

static trial_result_t results[LINE_COUNT][STATE_COUNT];

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

static uint32_t hold_reset_clocks(PIO pio, uint sm, uint clocks) {
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);

    uint32_t sample = sio_hw->gpio_in;
    for (uint i = 0; i < clocks; ++i) {
        sample = clock_step(pio, sm);
    }
    return sample;
}

static bool prepare_state(PIO pio,
                          uint sm,
                          diag_state_t state,
                          trial_result_t *r) {
    r->address = 0u;

    /* Every state starts from a validated RESET-high hold. */
    r->state_sample = hold_reset_clocks(pio, sm, DIAG_RESET_CLOCKS);

    if (state == STATE_RESET20) {
        return true;
    }

    if (state == STATE_RESET40) {
        for (uint i = 0; i < DIAG_RESET_CLOCKS; ++i) {
            r->state_sample = clock_step(pio, sm);
        }
        return true;
    }

    /* Release RESET while CLK is stalled LOW. */
    drive_cpu_input(V30_PIN_RESET, false);
    r->state_sample = sio_hw->gpio_in;

    if (state == STATE_RELEASE0) {
        return true;
    }

    /* Find the first valid reset-vector address after RESET release. */
    for (uint step = 0; step < DIAG_ALE_SEARCH_CLOCKS; ++step) {
        const uint32_t sample = clock_step(pio, sm);
        if (sample_bit(sample, V30_PIN_ALE) != 0u &&
            decode_address(sample) == DIAG_RESET_VECTOR) {
            r->address = DIAG_RESET_VECTOR;

            if (sample_bit(sample, V30_PIN_AD0) != 0u ||
                sample_bit(sample, V30_PIN_BHE) != 0u) {
                return false;
            }

            const uint32_t control = clock_step(pio, sm);
            if (sample_bit(control, V30_PIN_IOM) != 1u ||
                sample_bit(control, V30_PIN_DTR) != 0u ||
                sample_bit(control, V30_PIN_INTA) != 1u) {
                return false;
            }

            r->state_sample = control;
            return true;
        }
    }

    return false;
}

static void run_trial(PIO pio,
                      uint sm,
                      const line_desc_t *line,
                      diag_state_t state,
                      trial_result_t *r) {
    *r = (trial_result_t){0};
    r->state_ok = prepare_state(pio, sm, state, r);

    if (!r->state_ok) {
        (void)hold_reset_clocks(pio, sm, DIAG_RESET_CLOCKS);
        return;
    }

    const uint32_t mask = 1u << line->gpio;

    /* All AD pins are high-Z before probing this one line. */
    release_ad_bus();
    r->hiz_sample = sio_hw->gpio_in;

    /* Drive only the selected AD line HIGH and observe the digital pad. */
    sio_hw->gpio_set = mask;
    sio_hw->gpio_oe_set = mask;
    r->out_high = sio_hw->gpio_out;
    r->oe_high = sio_hw->gpio_oe;
    r->pad_high_0 = sio_hw->gpio_in;
    busy_wait_us_32(DIAG_SETTLE_US);
    r->pad_high_2 = sio_hw->gpio_in;

    /* Release immediately to minimize contention exposure. */
    sio_hw->gpio_oe_clr = mask;
    busy_wait_us_32(DIAG_SETTLE_US);
    r->release_sample = sio_hw->gpio_in;

    (void)hold_reset_clocks(pio, sm, DIAG_RESET_CLOCKS);
}

static void print_results(void) {
    printf("\n=== AD state-isolation results ===\n");
    printf("Each cell: HIZ -> OUT/OE/PAD0/PAD2 -> RELEASE | result\n\n");

    for (uint line_index = 0; line_index < LINE_COUNT; ++line_index) {
        const line_desc_t *line = &lines[line_index];
        printf("%s / GPIO%u\n", line->name, line->gpio);

        for (uint state_index = 0; state_index < STATE_COUNT; ++state_index) {
            const diag_state_t state = (diag_state_t)state_index;
            const trial_result_t *r = &results[line_index][state_index];

            printf("  %-39s : ", state_name(state));
            if (!r->state_ok) {
                printf("SETUP_FAIL");
                if (state == STATE_READ_DATA) {
                    printf(" addr=0x%05lX", (unsigned long)r->address);
                }
                printf("\n");
                continue;
            }

            const uint gpio = line->gpio;
            const uint32_t hiz = sample_bit(r->hiz_sample, gpio);
            const uint32_t out = sample_bit(r->out_high, gpio);
            const uint32_t oe = sample_bit(r->oe_high, gpio);
            const uint32_t pad0 = sample_bit(r->pad_high_0, gpio);
            const uint32_t pad2 = sample_bit(r->pad_high_2, gpio);
            const uint32_t rel = sample_bit(r->release_sample, gpio);
            const bool high_pass = out == 1u && oe == 1u && pad2 == 1u;

            printf("%lu -> %lu/%lu/%lu/%lu -> %lu | HIGH:%s",
                   (unsigned long)hiz,
                   (unsigned long)out,
                   (unsigned long)oe,
                   (unsigned long)pad0,
                   (unsigned long)pad2,
                   (unsigned long)rel,
                   high_pass ? "PASS" : "FAIL");

            if (state == STATE_READ_DATA) {
                printf(" addr=0x%05lX", (unsigned long)r->address);
            }
            printf("\n");
        }
        printf("\n");
    }

    printf("Interpretation:\n");
    printf("  PASS in RESET states but FAIL only in READ_DATA => failure depends on V30 bus state/ownership.\n");
    printf("  FAIL already with RESET held HIGH => investigate HAT/V30 electrical path rather than read timing.\n");
    printf("  AD6/AD8 controls distinguish selected-line behavior from a general diagnostic-path problem.\n");
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

    printf("\npi86-rp2350 Gate 4 AD state-isolation diagnostic\n");
    printf("No external oscilloscope is required: RP2350 SIO OUT/OE/PAD readback is used as the digital observer.\n");
    printf("Lines: AD4/GPIO5, AD7/GPIO9, AD6/GPIO11 control, AD8/GPIO10 control.\n");
    printf("States: RESET20, RESET40, RESET release before first clock, and first 0xFFFF0 read-data phase.\n");
    printf("Only one AD line is output-enabled at a time; all other AD pins remain high-Z.\n");
    printf("Drive-high exposure is limited to %u us before release. No PSRAM is used.\n\n",
           DIAG_SETTLE_US);

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

    for (uint line_index = 0; line_index < LINE_COUNT; ++line_index) {
        for (uint state_index = 0; state_index < STATE_COUNT; ++state_index) {
            run_trial(pio,
                      sm,
                      &lines[line_index],
                      (diag_state_t)state_index,
                      &results[line_index][state_index]);
        }
    }

    release_ad_bus();
    (void)hold_reset_clocks(pio, sm, DIAG_RESET_CLOCKS);
    stop_clock_low(pio, sm);

    print_results();
    printf("This diagnostic localizes state dependence; it does not declare Gate 4 PASS.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 AD-state heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
