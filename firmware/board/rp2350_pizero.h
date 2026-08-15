#pragma once

#ifndef WAVESHARE_RP2350_PIZERO
#error "Build pi86-rp2350 with PICO_BOARD=waveshare_rp2350_pizero (Pico SDK 2.3.0+)."
#endif

#define RP2350_PIZERO_HEADER_GPIO_FIRST 0u
#define RP2350_PIZERO_HEADER_GPIO_LAST  27u
#define RP2350_PIZERO_HEADER_GPIO_COUNT 28u

/*
 * GPIO0..GPIO27 are exposed on the Raspberry Pi-compatible 40-pin header.
 * This project preserves the original Pi86 V30 HAT pin assignment.
 *
 * The official Pico SDK waveshare_rp2350_pizero board definition selects:
 * - RP2350B (48-GPIO package)
 * - 16 MB onboard flash
 */
