#pragma once

#ifndef WAVESHARE_RP2350_PIZERO
#error "Build pi86-rp2350 with PICO_BOARD=waveshare_rp2350_pizero (Pico SDK 2.3.0+)."
#endif

#define RP2350_PIZERO_HEADER_GPIO_FIRST 0u
#define RP2350_PIZERO_HEADER_GPIO_LAST  27u
#define RP2350_PIZERO_HEADER_GPIO_COUNT 28u

/*
 * RP2350 GPIO0..GPIO27 are exposed across the Raspberry Pi-compatible
 * 40-pin header, but the RP2350 GPIO number at a physical header position is
 * not always equal to the Raspberry Pi BCM GPIO number for that position.
 *
 * Pi86 HAT signal definitions must therefore translate through the physical
 * 40-pin header position. See firmware/v30/v30_pins.h and docs/pin_mapping.md.
 *
 * The official Pico SDK waveshare_rp2350_pizero board definition selects:
 * - RP2350B (48-GPIO package)
 * - 16 MB onboard flash
 */
