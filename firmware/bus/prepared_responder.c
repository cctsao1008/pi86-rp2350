/*
 * Reusable PIO/DMA primitives for the prepared processor-bus responder.
 *
 * This module contains no validation entry point and owns no runtime policy.
 * It only translates the scattered physical bus, establishes safe GPIO
 * ownership, and moves immutable prepared words between SRAM and PIO.
 */

#include "bus/prepared_responder.h"

#include "bus/processor_bus_pins.h"
#include "bus_observer.pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "pico/stdlib.h"

#define RP86_PREPARED_SIGNAL_TIMEOUT_CLOCKS 64u

#define RP86_PREPARED_QUALIFIED_T1_CONTROL_BITS \
    ((1u << RP86_PROCESSOR_PIN_ASTB) |            \
     (1u << RP86_PROCESSOR_PIN_IOM) |             \
     (1u << RP86_PROCESSOR_PIN_INTAK))

static const uint8_t ad_pins[16] = {
    RP86_PROCESSOR_PIN_AD0, RP86_PROCESSOR_PIN_AD1,
    RP86_PROCESSOR_PIN_AD2, RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4, RP86_PROCESSOR_PIN_AD5,
    RP86_PROCESSOR_PIN_AD6, RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8, RP86_PROCESSOR_PIN_AD9,
    RP86_PROCESSOR_PIN_AD10, RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12, RP86_PROCESSOR_PIN_AD13,
    RP86_PROCESSOR_PIN_AD14, RP86_PROCESSOR_PIN_AD15,
};

uint32_t rp86_prepared_sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

uint16_t rp86_prepared_decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(rp86_prepared_sample_bit(sample, ad_pins[bit])
                            << bit);
    return value;
}

uint32_t rp86_prepared_decode_address(uint32_t sample) {
    uint32_t address = rp86_prepared_decode_ad(sample);
    address |= rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_A16) << 16;
    address |= rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_A17) << 17;
    address |= rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_A18) << 18;
    address |= rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_A19) << 19;
    return address & 0xfffffu;
}

bool rp86_prepared_is_memory_read(uint32_t sample) {
    return rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_IOM) != 0u &&
           rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_BUFRW) == 0u &&
           rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_INTAK) != 0u;
}

bool rp86_prepared_is_memory_write(uint32_t sample) {
    return rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_IOM) != 0u &&
           rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_BUFRW) != 0u &&
           rp86_prepared_sample_bit(sample, RP86_PROCESSOR_PIN_INTAK) != 0u;
}

uint32_t rp86_prepared_encode_word(uint16_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if ((value & (1u << bit)) != 0u) encoded |= 1u << ad_pins[bit];
    }
    return encoded;
}

uint32_t rp86_prepared_encode_address(uint32_t address) {
    uint32_t encoded = rp86_prepared_encode_word((uint16_t)address);
    if (address & (1u << 16)) encoded |= 1u << RP86_PROCESSOR_PIN_A16;
    if (address & (1u << 17)) encoded |= 1u << RP86_PROCESSOR_PIN_A17;
    if (address & (1u << 18)) encoded |= 1u << RP86_PROCESSOR_PIN_A18;
    if (address & (1u << 19)) encoded |= 1u << RP86_PROCESSOR_PIN_A19;
    return encoded;
}

uint32_t rp86_prepared_encode_drive(uint16_t value) {
    return rp86_prepared_encode_word(value) |
           (1u << RP86_PREPARED_RESPONSE_VALID_BIT);
}

uint32_t rp86_prepared_memory_read_key(uint32_t address) {
    return RP86_PREPARED_QUALIFIED_T1_CONTROL_BITS |
           rp86_prepared_encode_address(address);
}

void rp86_prepared_header_high_z(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

void rp86_prepared_control_outputs_init(void) {
    gpio_init(RP86_PROCESSOR_PIN_RESET);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_RESET);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);
    gpio_init(RP86_PROCESSOR_PIN_INTR);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_INTR);
    gpio_put(RP86_PROCESSOR_PIN_INTR, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_INTR, GPIO_OUT);
}

void rp86_prepared_route_ad_to_sio_high_z(void) {
    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_pins[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = RP86_PROCESSOR_AD_BUS_MASK;
}

void rp86_prepared_route_ad_to_responder(
    const rp86_prepared_sm_t *responder) {
    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(responder->pio, ad_pins[bit]);
}

bool rp86_prepared_non_ad_pins_isolated(
    const rp86_prepared_sm_t *responder) {
    const uint responder_func = pio_get_funcsel(responder->pio);
    for (uint gpio = RP86_PREPARED_OUT_BASE;
         gpio < RP86_PREPARED_OUT_BASE + RP86_PREPARED_OUT_COUNT; ++gpio) {
        if ((RP86_PROCESSOR_AD_BUS_MASK & (1u << gpio)) != 0u) continue;
        if ((uint)gpio_get_function(gpio) == responder_func) return false;
    }
    return true;
}

static uint64_t timeout_us_from_clocks(uint32_t clocks,
                                       uint32_t processor_hz) {
    return ((uint64_t)clocks * 1000000ull + processor_hz - 1u) /
           processor_hz + 2u;
}

bool rp86_prepared_wait_reset_clocks(uint count, uint32_t processor_hz) {
    const uint64_t edge_timeout = timeout_us_from_clocks(
        RP86_PREPARED_SIGNAL_TIMEOUT_CLOCKS, processor_hz);
    for (uint i = 0u; i < count; ++i) {
        uint64_t deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline &&
               !gpio_get(RP86_PROCESSOR_PIN_CLK))
            tight_loop_contents();
        if (!gpio_get(RP86_PROCESSOR_PIN_CLK)) return false;
        deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline &&
               gpio_get(RP86_PROCESSOR_PIN_CLK))
            tight_loop_contents();
        if (gpio_get(RP86_PROCESSOR_PIN_CLK)) return false;
    }
    return true;
}

void rp86_prepared_observer_init(rp86_prepared_sm_t *observer) {
    observer->pio = pio0;
    observer->sm = pio_claim_unused_sm(observer->pio, true);
    observer->offset = pio_add_program(observer->pio,
                                       &rp86_bus_observer_program);
    pio_sm_config config = rp86_bus_observer_program_get_default_config(
        observer->offset);
    sm_config_set_in_pins(&config, 0u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(observer->pio, observer->sm, observer->offset,
                            &config) == PICO_OK);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
}

void rp86_prepared_sm_arm(rp86_prepared_sm_t *sm) {
    pio_sm_set_enabled(sm->pio, sm->sm, false);
    pio_sm_clear_fifos(sm->pio, sm->sm);
    pio_sm_restart(sm->pio, sm->sm);
    pio_sm_exec(sm->pio, sm->sm, pio_encode_jmp(sm->offset));
}

int rp86_prepared_start_tx_dma(const rp86_prepared_sm_t *sm,
                               const uint32_t *words, uint32_t count) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config config = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, pio_get_dreq(sm->pio, sm->sm, true));
    channel_config_set_high_priority(&config, true);
    dma_channel_configure((uint)channel, &config, &sm->pio->txf[sm->sm],
                          words, count, true);
    return channel;
}

int rp86_prepared_start_observer_dma(const rp86_prepared_sm_t *observer,
                                     uint32_t *words, uint32_t count) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config config = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(
        &config, pio_get_dreq(observer->pio, observer->sm, false));
    channel_config_set_high_priority(&config, true);
    dma_channel_configure((uint)channel, &config, words,
                          &observer->pio->rxf[observer->sm], count, true);
    return channel;
}

uint32_t rp86_prepared_dma_remaining(int channel) {
    return channel < 0 ? 0u :
        dma_channel_hw_addr((uint)channel)->transfer_count & 0x0fffffffu;
}

bool rp86_prepared_wait_fifo_primed(const rp86_prepared_sm_t *sm,
                                    uint32_t level) {
    const uint64_t deadline = time_us_64() + 10000u;
    while (time_us_64() <= deadline) {
        if (pio_sm_get_tx_fifo_level(sm->pio, sm->sm) >= level) return true;
        tight_loop_contents();
    }
    return false;
}
