#pragma once

/*
 * Original Pi86/Homebrew8088 V20/V30 HAT mapping.
 * Values are BCM/RP2350 GPIO numbers on the Raspberry Pi-compatible header.
 * Do not renumber these pins without changing the hardware interface.
 */

#define V30_PIN_CLK    21u
#define V30_PIN_RESET  16u
#define V30_PIN_ALE    12u
#define V30_PIN_IOM     8u
#define V30_PIN_DTR     7u
#define V30_PIN_BHE    25u
#define V30_PIN_INTR   20u
#define V30_PIN_INTA    1u

#define V30_PIN_AD0    26u
#define V30_PIN_AD1    19u
#define V30_PIN_AD2    13u
#define V30_PIN_AD3     6u
#define V30_PIN_AD4     5u
#define V30_PIN_AD5     0u
#define V30_PIN_AD6    11u
#define V30_PIN_AD7     9u
#define V30_PIN_AD8    10u
#define V30_PIN_AD9    22u
#define V30_PIN_AD10   27u
#define V30_PIN_AD11   17u
#define V30_PIN_AD12    4u
#define V30_PIN_AD13    3u
#define V30_PIN_AD14    2u
#define V30_PIN_AD15   14u

#define V30_PIN_A16    15u
#define V30_PIN_A17    18u
#define V30_PIN_A18    23u
#define V30_PIN_A19    24u

#define V30_AD_BUS_MASK ((1u << V30_PIN_AD0)  | (1u << V30_PIN_AD1)  | \
                         (1u << V30_PIN_AD2)  | (1u << V30_PIN_AD3)  | \
                         (1u << V30_PIN_AD4)  | (1u << V30_PIN_AD5)  | \
                         (1u << V30_PIN_AD6)  | (1u << V30_PIN_AD7)  | \
                         (1u << V30_PIN_AD8)  | (1u << V30_PIN_AD9)  | \
                         (1u << V30_PIN_AD10) | (1u << V30_PIN_AD11) | \
                         (1u << V30_PIN_AD12) | (1u << V30_PIN_AD13) | \
                         (1u << V30_PIN_AD14) | (1u << V30_PIN_AD15))
