#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/regs/pio.h"
#include "hardware/structs/sio.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "board/rp2350_pizero.h"
#include "v30/v30_pins.h"
#include "gate4_step_clock.pio.h"
#include "gate4_ale_reset_capture.pio.h"

#define STEP_PIO_CLOCK_HZ          2000000u
#define CAPTURE_PIO_CLOCK_HZ       8000000u
#define RESET_HOLD_CLOCKS               20u
#define CAPTURE_PRE_RESET_CLOCKS         2u
#define CAPTURE_POST_RESET_CLOCKS        9u
#define CAPTURE_SAMPLES                192u
#define SAMPLE_INTERVAL_US              0.5
#define RESET_VECTOR                0xFFFF0u

static uint32_t dma_words[CAPTURE_SAMPLES];
static uint32_t samples[CAPTURE_SAMPLES];

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

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

static uint32_t decode_a19_16(uint32_t sample) {
    return (sample_bit(sample, V30_PIN_A16) << 0) |
           (sample_bit(sample, V30_PIN_A17) << 1) |
           (sample_bit(sample, V30_PIN_A18) << 2) |
           (sample_bit(sample, V30_PIN_A19) << 3);
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
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (float)STEP_PIO_CLOCK_HZ);

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

static void init_capture_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = gate4_ale_reset_capture_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (float)CAPTURE_PIO_CLOCK_HZ);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_set_enabled(pio, sm, false);
}

static void append_mark(char *dst, size_t dst_size, const char *text) {
    if (dst[0] != '\0') {
        strncat(dst, "+", dst_size - strlen(dst) - 1u);
    }
    strncat(dst, text, dst_size - strlen(dst) - 1u);
}

static void print_capture(bool rx_stall) {
    int reset_fall_idx = -1;
    int first_ffff0_idx = -1;
    int first_ale_fall_after_ffff0 = -1;
    uint ale_rises = 0;
    uint ale_falls = 0;

    printf("\n=== DMA reset/ALE capture ===\n");
    printf("PIO samples continuously; no WAIT instruction is used for RESET or ALE.\n");
    printf("DMA drains the PIO RX FIFO so CPU print/drain timing cannot pace sampling.\n");
    printf("Nominal sample interval = %.1f us. AD bus is high-Z throughout.\n\n",
           SAMPLE_INTERVAL_US);
    printf(" IDX   us   RST CLK ALE IOM DTR BHE INTA  AD     A19:16 ADDR   AD4 AD7  MARK\n");
    printf(" --- ------ --- --- --- --- --- --- ---- ------ ------- -----  --- ---  ----------------\n");

    for (uint i = 0; i < CAPTURE_SAMPLES; ++i) {
        const uint32_t s = samples[i];
        const uint32_t prev = i == 0u ? s : samples[i - 1u];
        const bool rst = sample_bit(s, V30_PIN_RESET) != 0u;
        const bool prev_rst = sample_bit(prev, V30_PIN_RESET) != 0u;
        const bool ale = sample_bit(s, V30_PIN_ALE) != 0u;
        const bool prev_ale = sample_bit(prev, V30_PIN_ALE) != 0u;
        const uint32_t addr = decode_address(s);
        const bool ffff0 = ale && addr == RESET_VECTOR;

        char mark[64] = "";
        if (i == 0u) {
            append_mark(mark, sizeof(mark), "START");
        }
        if (prev_rst && !rst) {
            append_mark(mark, sizeof(mark), "RESET_FALL");
            if (reset_fall_idx < 0) {
                reset_fall_idx = (int)i;
            }
        }
        if (!prev_ale && ale) {
            append_mark(mark, sizeof(mark), "ALE_RISE");
            ++ale_rises;
        }
        if (prev_ale && !ale) {
            append_mark(mark, sizeof(mark), "ALE_FALL");
            ++ale_falls;
            if (first_ffff0_idx >= 0 && first_ale_fall_after_ffff0 < 0) {
                first_ale_fall_after_ffff0 = (int)i;
            }
        }
        if (ffff0) {
            append_mark(mark, sizeof(mark), "FFFF0");
            if (first_ffff0_idx < 0) {
                first_ffff0_idx = (int)i;
            }
        }

        printf(" %3u %6.1f  %u   %u   %u   %u   %u   %u    %u   %04X     %lX   %05lX   %u   %u  %s\n",
               i,
               (double)i * SAMPLE_INTERVAL_US,
               (unsigned)rst,
               (unsigned)sample_bit(s, V30_PIN_CLK),
               (unsigned)ale,
               (unsigned)sample_bit(s, V30_PIN_IOM),
               (unsigned)sample_bit(s, V30_PIN_DTR),
               (unsigned)sample_bit(s, V30_PIN_BHE),
               (unsigned)sample_bit(s, V30_PIN_INTA),
               decode_ad(s),
               (unsigned long)decode_a19_16(s),
               (unsigned long)addr,
               (unsigned)sample_bit(s, V30_PIN_AD4),
               (unsigned)sample_bit(s, V30_PIN_AD7),
               mark);
    }

    printf("\nCapture integrity / transition summary:\n");
    printf("  DMA samples captured        = %u / %u\n", CAPTURE_SAMPLES, CAPTURE_SAMPLES);
    printf("  PIO RXSTALL observed        = %s\n", rx_stall ? "YES (TIMING INVALID)" : "NO");
    printf("  RESET falling edge index    = %d\n", reset_fall_idx);
    printf("  first FFFF0/ALE index       = %d\n", first_ffff0_idx);
    printf("  first ALE fall after FFFF0  = %d\n", first_ale_fall_after_ffff0);
    printf("  ALE rises / falls captured  = %u / %u\n", ale_rises, ale_falls);

    if (reset_fall_idx < 0) {
        printf("  RESULT: INVALID - RESET falling edge was not captured.\n");
    } else if (rx_stall) {
        printf("  RESULT: INVALID - capture cadence was disturbed by RX FIFO stall.\n");
    } else {
        printf("  RESULT: VALID CAPTURE - interpret ALE only from the recorded transitions above.\n");
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

    printf("\npi86-rp2350 Gate 4 DMA reset/ALE capture\n");
    printf("Purpose: verify RESET and ALE/ASTB transitions without assuming an ALE trigger.\n");
    printf("Capture path: free-running PIO -> DMA -> RAM. CPU does not drain samples in real time.\n");
    printf("The capture begins while RESET is HIGH, includes RESET falling edge, then continues post-release.\n");
    printf("AD bus remains high-Z for the entire capture. No PSRAM is used.\n\n");

    for (int seconds = 3; seconds >= 1; --seconds) {
        printf("Capture starts in %d second%s...\n",
               seconds,
               seconds == 1 ? "" : "s");
        fflush(stdout);
        sleep_ms(1000);
    }

    PIO step_pio = pio0;
    const uint step_sm = pio_claim_unused_sm(step_pio, true);
    const uint step_offset = pio_add_program(step_pio, &gate4_step_clk_program);
    init_step_clock(step_pio, step_sm, step_offset);

    /* Establish RESET independently before the measurement begins. */
    for (uint i = 0; i < RESET_HOLD_CLOCKS; ++i) {
        (void)clock_step(step_pio, step_sm);
    }

    PIO cap_pio = pio1;
    const uint cap_sm = pio_claim_unused_sm(cap_pio, true);
    const uint cap_offset = pio_add_program(cap_pio, &gate4_ale_reset_capture_program);
    init_capture_sm(cap_pio, cap_sm, cap_offset);

    const int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(cap_pio, cap_sm, false));

    dma_channel_configure(
        dma_chan,
        &dc,
        dma_words,
        &cap_pio->rxf[cap_sm],
        CAPTURE_SAMPLES,
        false);

    /* Clear sticky FIFO-debug flags so RXSTALL is meaningful for this run only. */
    cap_pio->fdebug = 0xffffffffu;
    pio_sm_clear_fifos(cap_pio, cap_sm);
    pio_sm_restart(cap_pio, cap_sm);

    /* DMA is armed before the sampler starts; PIO therefore never relies on CPU draining. */
    dma_start_channel_mask(1u << (uint)dma_chan);
    pio_sm_set_enabled(cap_pio, cap_sm, true);

    /* Capture a known RESET-high prefix, then the actual RESET falling edge. */
    for (uint i = 0; i < CAPTURE_PRE_RESET_CLOCKS; ++i) {
        (void)clock_step(step_pio, step_sm);
    }

    drive_cpu_input(V30_PIN_RESET, false);

    for (uint i = 0; i < CAPTURE_POST_RESET_CLOCKS; ++i) {
        (void)clock_step(step_pio, step_sm);
    }

    dma_channel_wait_for_finish_blocking(dma_chan);
    pio_sm_set_enabled(cap_pio, cap_sm, false);

    const bool rx_stall =
        ((cap_pio->fdebug >> (PIO_FDEBUG_RXSTALL_LSB + cap_sm)) & 1u) != 0u;

    for (uint i = 0; i < CAPTURE_SAMPLES; ++i) {
        samples[i] = dma_words[i] >> 4;
    }

    /* Return the CPU to the same safe halted state used by earlier diagnostics. */
    release_ad_bus();
    drive_cpu_input(V30_PIN_RESET, true);
    for (uint i = 0; i < RESET_HOLD_CLOCKS; ++i) {
        (void)clock_step(step_pio, step_sm);
    }
    stop_clock_low(step_pio, step_sm);
    dma_channel_unclaim(dma_chan);

    print_capture(rx_stall);
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    uint32_t heartbeat = 0;
    while (true) {
        if (stdio_usb_connected()) {
            printf("Gate 4 DMA ALE heartbeat %lu | RESET=1 CLK=0 AD=Hi-Z\n",
                   (unsigned long)heartbeat++);
            fflush(stdout);
        }
        sleep_ms(1000);
    }
}
