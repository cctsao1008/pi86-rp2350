#pragma once

/*
 * Original Pi86/Homebrew8088 V20/V30 HAT mapping.
 *
 * Canonical mapping rule:
 *   V30 signal -> Raspberry Pi physical 40-pin header position
 *              -> Waveshare RP2350-PiZero GPIO number
 *
 * IMPORTANT:
 *   Raspberry Pi BCM GPIO numbers are NOT the same as RP2350-PiZero GPIO
 *   numbers for every physical header position.  Do not translate the HAT
 *   wiring by copying BCM numbers directly into RP2350 GPIO definitions.
 *
 * The physical Raspberry Pi header position is the hardware ABI.  The values
 * below are the RP2350 GPIO numbers actually routed to those positions on the
 * RP2350-PiZero.
 */

/* Control signals. */
#define V30_PIN_CLK    21u  /* RPi physical pin 40 */
#define V30_PIN_RESET  16u  /* RPi physical pin 36 */
#define V30_PIN_ALE     9u  /* RPi physical pin 32 */
#define V30_PIN_IOM     8u  /* RPi physical pin 24 */
#define V30_PIN_BUFRW   7u  /* RPi physical pin 26 */
#define V30_PIN_DTR V30_PIN_BUFRW /* Legacy firmware name. */
#define V30_PIN_BHE    25u  /* RPi physical pin 22 */
#define V30_PIN_INTR   20u  /* RPi physical pin 38 */
#define V30_PIN_INTA    1u  /* RPi physical pin 28 */

/* Multiplexed address/data bus. */
#define V30_PIN_AD0    26u  /* RPi physical pin 37 */
#define V30_PIN_AD1    19u  /* RPi physical pin 35 */
#define V30_PIN_AD2    13u  /* RPi physical pin 33 */
#define V30_PIN_AD3     6u  /* RPi physical pin 31 */
#define V30_PIN_AD4    15u  /* RPi physical pin 29 */
#define V30_PIN_AD5     0u  /* RPi physical pin 27 */
#define V30_PIN_AD6    10u  /* RPi physical pin 23 */
#define V30_PIN_AD7    12u  /* RPi physical pin 21 */
#define V30_PIN_AD8    11u  /* RPi physical pin 19 */
#define V30_PIN_AD9    22u  /* RPi physical pin 15 */
#define V30_PIN_AD10   27u  /* RPi physical pin 13 */
#define V30_PIN_AD11   17u  /* RPi physical pin 11 */
#define V30_PIN_AD12   14u  /* RPi physical pin 7  */
#define V30_PIN_AD13    3u  /* RPi physical pin 5  */
#define V30_PIN_AD14    2u  /* RPi physical pin 3  */
#define V30_PIN_AD15    4u  /* RPi physical pin 8  */

/* High address bus. */
#define V30_PIN_A16     5u  /* RPi physical pin 10 */
#define V30_PIN_A17    18u  /* RPi physical pin 12 */
#define V30_PIN_A18    23u  /* RPi physical pin 16 */
#define V30_PIN_A19    24u  /* RPi physical pin 18 */

#define V30_AD_BUS_MASK ((1u << V30_PIN_AD0)  | (1u << V30_PIN_AD1)  | \
                         (1u << V30_PIN_AD2)  | (1u << V30_PIN_AD3)  | \
                         (1u << V30_PIN_AD4)  | (1u << V30_PIN_AD5)  | \
                         (1u << V30_PIN_AD6)  | (1u << V30_PIN_AD7)  | \
                         (1u << V30_PIN_AD8)  | (1u << V30_PIN_AD9)  | \
                         (1u << V30_PIN_AD10) | (1u << V30_PIN_AD11) | \
                         (1u << V30_PIN_AD12) | (1u << V30_PIN_AD13) | \
                         (1u << V30_PIN_AD14) | (1u << V30_PIN_AD15))
