#pragma once

#include "pico.h"

#ifndef WAVESHARE_RP2350_PIZERO
#error "Build pi86-rp2350 with PICO_BOARD=waveshare_rp2350_pizero (Pico SDK 2.3.0+)."
#endif

#define RP2350_PIZERO_HEADER_GPIO_FIRST 0u
#define RP2350_PIZERO_HEADER_GPIO_LAST  27u
#define RP2350_PIZERO_HEADER_GPIO_COUNT 28u

/* Onboard interfaces occupy the RP2350B-only GPIO bank above the 40-pin
 * header. These values come from the Waveshare RP2350-PiZero schematic.
 * Canonical firmware initializes every pin below to passive input before a
 * service is allowed to claim it. */
#define RP2350_PIZERO_PIO_USB_DP 28u
#define RP2350_PIZERO_PIO_USB_DM 29u

#define RP2350_PIZERO_SD_CLK     30u
#define RP2350_PIZERO_SD_CMD     31u
#define RP2350_PIZERO_SD_D0      40u
#define RP2350_PIZERO_SD_D1      41u
#define RP2350_PIZERO_SD_D2      42u
#define RP2350_PIZERO_SD_D3      43u
/* SPI aliases supported by the board wiring. */
#define RP2350_PIZERO_SD_MOSI    RP2350_PIZERO_SD_CMD
#define RP2350_PIZERO_SD_MISO    RP2350_PIZERO_SD_D0
#define RP2350_PIZERO_SD_CS      RP2350_PIZERO_SD_D3

#define RP2350_PIZERO_DVI_D2_P   32u
#define RP2350_PIZERO_DVI_D2_N   33u
#define RP2350_PIZERO_DVI_D1_P   34u
#define RP2350_PIZERO_DVI_D1_N   35u
#define RP2350_PIZERO_DVI_D0_P   36u
#define RP2350_PIZERO_DVI_D0_N   37u
#define RP2350_PIZERO_DVI_CLK_P  38u
#define RP2350_PIZERO_DVI_CLK_N  39u
#define RP2350_PIZERO_DVI_SDA    44u
#define RP2350_PIZERO_DVI_SCL    45u
#define RP2350_PIZERO_DVI_CEC    46u

#define RP2350_PIZERO_PSRAM_CS   47u

/*
 * RP2350 GPIO0..GPIO27 are exposed across the Raspberry Pi-compatible
 * 40-pin header, but the RP2350 GPIO number at a physical header position is
 * not always equal to the Raspberry Pi BCM GPIO number for that position.
 *
 * Pi86 HAT signal definitions must therefore translate through the physical
 * 40-pin header position. See firmware/processor/processor_bus_pins.h and docs/pin_mapping.md.
 *
 * The official Pico SDK waveshare_rp2350_pizero board definition selects:
 * - RP2350B (48-GPIO package)
 * - 16 MB onboard flash
 */
