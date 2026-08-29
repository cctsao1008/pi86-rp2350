#ifndef RP86_RP2350_PIZERO_RESOURCES_H
#define RP86_RP2350_PIZERO_RESOURCES_H

#include <stdbool.h>

typedef struct {
    bool safe_state_initialized;
    bool microsd_claimed;
    bool dvi_claimed;
    bool pio_usb_claimed;
} rp86_board_resources_t;

/* Establishes the canonical power-on contract for onboard interfaces:
 * MicroSD, Mini HDMI/DVI and PIO-USB are all passive inputs with no RP2350
 * pulls or output enables. External board resistors remain authoritative.
 * A future service must explicitly claim and reconfigure its pins. */
void rp86_board_resources_safe_init(rp86_board_resources_t *resources);

/* DVI and PIO-USB compete for realtime PIO resources on this board and are
 * intentionally modelled as mutually exclusive runtime claims. */
bool rp86_board_resources_can_claim_dvi(
    const rp86_board_resources_t *resources);
bool rp86_board_resources_can_claim_pio_usb(
    const rp86_board_resources_t *resources);

#endif
