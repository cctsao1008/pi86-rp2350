#include "board/rp2350_pizero_resources.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board/rp2350_pizero.h"
#include "hardware/gpio.h"

static const uint8_t microsd_pins[] = {
    RP2350_PIZERO_SD_CLK,
    RP2350_PIZERO_SD_CMD,
    RP2350_PIZERO_SD_D0,
    RP2350_PIZERO_SD_D1,
    RP2350_PIZERO_SD_D2,
    RP2350_PIZERO_SD_D3,
};

static const uint8_t dvi_pins[] = {
    RP2350_PIZERO_DVI_D2_P,
    RP2350_PIZERO_DVI_D2_N,
    RP2350_PIZERO_DVI_D1_P,
    RP2350_PIZERO_DVI_D1_N,
    RP2350_PIZERO_DVI_D0_P,
    RP2350_PIZERO_DVI_D0_N,
    RP2350_PIZERO_DVI_CLK_P,
    RP2350_PIZERO_DVI_CLK_N,
    RP2350_PIZERO_DVI_SDA,
    RP2350_PIZERO_DVI_SCL,
    RP2350_PIZERO_DVI_CEC,
};

static const uint8_t pio_usb_pins[] = {
    RP2350_PIZERO_PIO_USB_DP,
    RP2350_PIZERO_PIO_USB_DM,
};

static void make_passive(const uint8_t *pins, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_disable_pulls(pins[i]);
    }
}

void rp86_board_resources_safe_init(rp86_board_resources_t *resources) {
    memset(resources, 0, sizeof *resources);
    make_passive(microsd_pins, sizeof microsd_pins / sizeof microsd_pins[0]);
    make_passive(dvi_pins, sizeof dvi_pins / sizeof dvi_pins[0]);
    make_passive(pio_usb_pins,
                 sizeof pio_usb_pins / sizeof pio_usb_pins[0]);
    resources->safe_state_initialized = true;
}

bool rp86_board_resources_can_claim_dvi(
    const rp86_board_resources_t *resources) {
    return resources->safe_state_initialized && !resources->pio_usb_claimed;
}

bool rp86_board_resources_can_claim_pio_usb(
    const rp86_board_resources_t *resources) {
    return resources->safe_state_initialized && !resources->dvi_claimed;
}
