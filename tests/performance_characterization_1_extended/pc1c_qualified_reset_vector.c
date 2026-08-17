/*
 * PC1-C0B address-qualified reset-vector response at 0.300 MHz.
 *
 * PIO1 owns the synchronized clock, T1 capture, and AD response state machines.
 * The M33 classifies each current-cycle snapshot and performs an internal-SRAM
 * ROM lookup. PIO0 remains passive; DMA drains its address observer into SRAM.
 * Observing a fetch at F0000 is the CPU-visible far-jump discriminator.
 */

#include <stdbool.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "v30/v30_pins.h"
#include "pc1b_first_cycle_phase_capture.pio.h"
#include "pc1c_early_cycle_capture.pio.h"
#include "pc1c_qualified_ad.pio.h"
#include "perf_ale_observer.pio.h"
#include "perf_continuous_clock.pio.h"

#define PC1C0B_V30_HZ             300000u
#define RESET_CLOCKS                  20u
#define SIGNAL_TIMEOUT_CLOCKS         64u
#define RUN_TIMEOUT_CLOCKS           640u
#define TRACE_DEPTH                   16u
#define FIRST_PHASE_COUNT              6u
#define OUT_BASE                       0u
#define OUT_COUNT                     28u
#define RESPONSE_VALID_BIT            28u
#define RESET_ROM_BASE           0xFFFF0u
#define RESET_ROM_SIZE                 6u
#define FAR_TARGET                0xF0000u

typedef enum {
    LANES_NONE = 0,
    LANE_LOW = 1,
    LANE_HIGH = 2,
    LANES_WORD = 3,
} lane_mask_t;

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} pc1c_sm_t;

typedef struct {
    uint32_t cycle_raw;
    uint32_t address;
    uint16_t response;
    lane_mask_t lanes;
    bool memory_read;
    bool rom_hit;
} trace_entry_t;

typedef struct {
    bool reset_ok;
    bool pre_release_clean;
    bool first_address_ok;
    bool first_memory_read;
    bool first_response_phase_ok;
    bool dma_observer_first_ok;
    bool far_target_seen;
    bool terminal_safe;
    uint32_t service_cycles;
    uint32_t capture_phase_errors;
    uint32_t rom_hits;
    uint32_t unsupported_cycles;
    uint32_t deadline_misses;
    uint32_t late_response_commands;
    uint32_t tx_backpressure;
    uint32_t unqualified_drive_commands;
    uint32_t observer_dma_pre;
    uint32_t observer_dma_post;
    uint32_t observer_fifo_residue;
    uint8_t required_hit_mask;
    trace_entry_t trace[TRACE_DEPTH];
    uint trace_count;
    uint32_t phase_raw[FIRST_PHASE_COUNT];
    uint phase_count;
    uint32_t observer_raw[TRACE_DEPTH];
    uint observer_count;
} pc1c0b_result_t;

static uint8_t g_reset_rom[RESET_ROM_SIZE];
static uint32_t g_observer_dma_words[TRACE_DEPTH];

static const uint8_t ad_pins[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static inline uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(sample, ad_pins[bit]) << bit);
    return value;
}

static inline uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static inline lane_mask_t decode_lanes(uint32_t cycle_raw) {
    const bool a0 = sample_bit(cycle_raw, V30_PIN_AD0) != 0u;
    const bool ube_n = sample_bit(cycle_raw, V30_PIN_UBE) != 0u;
    if (!a0 && !ube_n) return LANES_WORD;
    if (!a0 && ube_n) return LANE_LOW;
    if (a0 && !ube_n) return LANE_HIGH;
    return LANES_NONE;
}

static inline bool is_memory_read(uint32_t control_raw) {
    /* V30 minimum-mode bus: IO/M=1, BUFR/W=0, INTAK inactive. */
    return sample_bit(control_raw, V30_PIN_IOM) != 0u &&
           sample_bit(control_raw, V30_PIN_BUFRW) == 0u &&
           sample_bit(control_raw, V30_PIN_INTAK) != 0u;
}

static inline uint32_t encode_gpio_word(uint16_t value) {
    uint32_t encoded = 0u;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if ((value & (1u << bit)) != 0u)
            encoded |= 1u << ad_pins[bit];
    }
    return encoded;
}

static inline bool reset_rom_read(uint32_t address,
                                  lane_mask_t lanes,
                                  uint16_t *value) {
    if (lanes == LANES_WORD) {
        if (address < RESET_ROM_BASE ||
            address + 1u >= RESET_ROM_BASE + RESET_ROM_SIZE)
            return false;
        const uint32_t offset = address - RESET_ROM_BASE;
        *value = (uint16_t)g_reset_rom[offset] |
                 ((uint16_t)g_reset_rom[offset + 1u] << 8);
        return true;
    }

    if (address < RESET_ROM_BASE ||
        address >= RESET_ROM_BASE + RESET_ROM_SIZE)
        return false;

    const uint8_t byte = g_reset_rom[address - RESET_ROM_BASE];
    if (lanes == LANE_LOW) {
        *value = byte;
        return true;
    }
    if (lanes == LANE_HIGH) {
        *value = (uint16_t)byte << 8;
        return true;
    }
    return false;
}

static inline uint64_t timeout_us_from_clocks(uint32_t clocks) {
    return ((uint64_t)clocks * 1000000ull + PC1C0B_V30_HZ - 1u) /
           PC1C0B_V30_HZ + 2u;
}

static void prepare_header_high_z(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }
}

static void init_control_outputs(void) {
    gpio_init(V30_PIN_RESET);
    gpio_disable_pulls(V30_PIN_RESET);
    gpio_put(V30_PIN_RESET, true);
    gpio_set_dir(V30_PIN_RESET, GPIO_OUT);
    gpio_init(V30_PIN_INTR);
    gpio_disable_pulls(V30_PIN_INTR);
    gpio_put(V30_PIN_INTR, false);
    gpio_set_dir(V30_PIN_INTR, GPIO_OUT);
}

static void route_ad_to_sio_high_z(void) {
    for (uint bit = 0u; bit < 16u; ++bit)
        gpio_set_function(ad_pins[bit], GPIO_FUNC_SIO);
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static bool ad_is_sio_high_z(void) {
    if ((sio_hw->gpio_oe & V30_AD_BUS_MASK) != 0u) return false;
    for (uint bit = 0u; bit < 16u; ++bit) {
        if (gpio_get_function(ad_pins[bit]) != GPIO_FUNC_SIO) return false;
    }
    return true;
}

static void route_ad_to_responder(const pc1c_sm_t *responder) {
    for (uint bit = 0u; bit < 16u; ++bit)
        pio_gpio_init(responder->pio, ad_pins[bit]);
}

static void clock_init(pc1c_sm_t *clock) {
    clock->pio = pio1;
    clock->sm = pio_claim_unused_sm(clock->pio, true);
    clock->offset = pio_add_program(clock->pio, &perf_continuous_clk_program);
}

static void clock_prepare_300khz(pc1c_sm_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
    pio_sm_config c = perf_continuous_clk_program_get_default_config(clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)PC1C0B_V30_HZ));
    pio_gpio_init(clock->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm, V30_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(clock->pio, clock->sm, clock->offset, &c) == PICO_OK);
    pio_sm_set_pins_with_mask(clock->pio, clock->sm, 0u,
                              1u << V30_PIN_CLK);
}

static void clock_start_300khz(pc1c_sm_t *clock) {
    clock_prepare_300khz(clock);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
}

static void clock_stop_low(pc1c_sm_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t edge_timeout = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    for (uint i = 0u; i < count; ++i) {
        uint64_t deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline && !gpio_get(V30_PIN_CLK))
            tight_loop_contents();
        if (!gpio_get(V30_PIN_CLK)) return false;
        deadline = time_us_64() + edge_timeout;
        while (time_us_64() <= deadline && gpio_get(V30_PIN_CLK))
            tight_loop_contents();
        if (gpio_get(V30_PIN_CLK)) return false;
    }
    return true;
}

static void bus_capture_init(pc1c_sm_t *capture) {
    capture->pio = pio1;
    capture->sm = pio_claim_unused_sm(capture->pio, true);
    capture->offset = pio_add_program(capture->pio,
                                      &pc1c_early_cycle_capture_program);
    pio_sm_config c =
        pc1c_early_cycle_capture_program_get_default_config(capture->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ASTB);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(capture->pio, capture->sm, capture->offset, &c) == PICO_OK);
    pio_sm_set_enabled(capture->pio, capture->sm, false);
}

static void phase_capture_init(pc1c_sm_t *phase) {
    phase->pio = pio0;
    phase->sm = pio_claim_unused_sm(phase->pio, true);
    phase->offset = pio_add_program(phase->pio, &pc1b_first_cycle_phase_capture_program);
    pio_sm_config c = pc1b_first_cycle_phase_capture_program_get_default_config(phase->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(phase->pio, phase->sm, phase->offset, &c) == PICO_OK);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
}

static void observer_init(pc1c_sm_t *observer) {
    observer->pio = pio0;
    observer->sm = pio_claim_unused_sm(observer->pio, true);
    observer->offset = pio_add_program(observer->pio,
                                       &perf_ale_observer_program);
    pio_sm_config c =
        perf_ale_observer_program_get_default_config(observer->offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_jmp_pin(&c, V30_PIN_ASTB);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    hard_assert(pio_sm_init(observer->pio, observer->sm,
                            observer->offset, &c) == PICO_OK);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
}

static int start_observer_dma(const pc1c_sm_t *observer) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c,
                            pio_get_dreq(observer->pio, observer->sm, false));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c, g_observer_dma_words,
                          &observer->pio->rxf[observer->sm],
                          TRACE_DEPTH, true);
    return channel;
}

static uint32_t dma_remaining(int channel) {
    return dma_channel_hw_addr((uint)channel)->transfer_count & 0x0FFFFFFFu;
}

static void responder_init(pc1c_sm_t *responder) {
    responder->pio = pio1;
    responder->sm = pio_claim_unused_sm(responder->pio, true);
    responder->offset = pio_add_program(responder->pio, &pc1c_qualified_ad_program);
    pio_sm_config c = pc1c_qualified_ad_program_get_default_config(responder->offset);
    sm_config_set_out_pins(&c, OUT_BASE, OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    hard_assert(pio_sm_init(responder->pio, responder->sm, responder->offset, &c) == PICO_OK);
    pio_sm_set_enabled(responder->pio, responder->sm, false);
}

static void arm_sm(pc1c_sm_t *sm) {
    pio_sm_set_enabled(sm->pio, sm->sm, false);
    pio_sm_clear_fifos(sm->pio, sm->sm);
    pio_sm_restart(sm->pio, sm->sm);
    pio_sm_exec(sm->pio, sm->sm, pio_encode_jmp(sm->offset));
}

static void responder_preserve_clock_direction(pc1c_sm_t *responder) {
    pio_sm_put_blocking(responder->pio, responder->sm,
                        1u << (V30_PIN_CLK - OUT_BASE));
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_pull(false, false));
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_mov(pio_y, pio_osr));
}

static inline uint32_t response_command(uint16_t response, bool drive) {
    return encode_gpio_word(response) |
           (drive ? (1u << RESPONSE_VALID_BIT) : 0u);
}

static inline void remember_trace(pc1c0b_result_t *result,
                                  uint32_t cycle_raw,
                                  uint32_t address,
                                  lane_mask_t lanes,
                                  bool memory_read,
                                  bool rom_hit,
                                  uint16_t response) {
    if (result->trace_count >= TRACE_DEPTH) return;
    trace_entry_t *entry = &result->trace[result->trace_count++];
    *entry = (trace_entry_t){
        .cycle_raw = cycle_raw,
        .address = address,
        .response = response,
        .lanes = lanes,
        .memory_read = memory_read,
        .rom_hit = rom_hit,
    };
}

static void __not_in_flash_func(service_bus)(pc1c_sm_t *capture,
                                              pc1c_sm_t *responder,
                                              pc1c0b_result_t *result,
                                              uint64_t deadline) {
    while (time_us_64() <= deadline && !result->far_target_seen) {
        if (pio_sm_is_rx_fifo_empty(capture->pio, capture->sm)) {
            tight_loop_contents();
            continue;
        }

        const uint32_t cycle_raw = pio_sm_get(capture->pio, capture->sm);
        if (!sample_bit(cycle_raw, V30_PIN_ASTB) ||
            !sample_bit(cycle_raw, V30_PIN_CLK))
            ++result->capture_phase_errors;
        const uint32_t address = decode_address(cycle_raw);
        const lane_mask_t lanes = decode_lanes(cycle_raw);
        const bool memory_read = is_memory_read(cycle_raw);
        uint16_t response = 0u;
        const bool rom_hit = memory_read &&
            reset_rom_read(address, lanes, &response);

        if (pio_sm_is_tx_fifo_full(responder->pio, responder->sm)) {
            ++result->tx_backpressure;
            return;
        }
        pio_sm_put(responder->pio, responder->sm,
                   response_command(response, rom_hit));
        if (!gpio_get(V30_PIN_ASTB))
            ++result->late_response_commands;

        ++result->service_cycles;
        if (rom_hit) {
            ++result->rom_hits;
            if (address == 0xFFFF0u) result->required_hit_mask |= 1u << 0;
            if (address == 0xFFFF2u) result->required_hit_mask |= 1u << 1;
            if (address == 0xFFFF4u) result->required_hit_mask |= 1u << 2;
        } else {
            ++result->unsupported_cycles;
        }
        if (rom_hit && !memory_read)
            ++result->unqualified_drive_commands;

        remember_trace(result, cycle_raw, address, lanes,
                       memory_read, rom_hit, response);

        if (memory_read && address == FAR_TARGET)
            result->far_target_seen = true;
    }
}

static void run_test(pc1c_sm_t *clock,
                     pc1c_sm_t *capture,
                     pc1c_sm_t *phase,
                     pc1c_sm_t *observer,
                     pc1c_sm_t *responder,
                     pc1c0b_result_t *result) {
    *result = (pc1c0b_result_t){0};
    for (uint i = 0u; i < TRACE_DEPTH; ++i)
        g_observer_dma_words[i] = 0u;
    gpio_put(V30_PIN_INTR, false);
    gpio_put(V30_PIN_RESET, true);
    route_ad_to_sio_high_z();

    clock_start_300khz(clock);
    result->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    clock_stop_low(clock);

    clock_prepare_300khz(clock);
    arm_sm(capture);
    arm_sm(phase);
    arm_sm(observer);
    arm_sm(responder);
    responder_preserve_clock_direction(responder);
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));
    route_ad_to_responder(responder);
    const int observer_dma = start_observer_dma(observer);
    result->observer_dma_pre = dma_remaining(observer_dma);
    result->pre_release_clean =
        pio_sm_is_rx_fifo_empty(capture->pio, capture->sm) &&
        pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
        pio_sm_is_rx_fifo_empty(observer->pio, observer->sm) &&
        pio_sm_is_tx_fifo_empty(responder->pio, responder->sm) &&
        result->observer_dma_pre == TRACE_DEPTH &&
        !gpio_get(V30_PIN_CLK) &&
        (sio_hw->gpio_oe & V30_AD_BUS_MASK) == 0u;

    pio_enable_sm_mask_in_sync(pio0,
        (1u << phase->sm) | (1u << observer->sm));

    if (result->reset_ok && result->pre_release_clean) {
        const uint32_t irq_state = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_enable_sm_mask_in_sync(pio1,
            (1u << clock->sm) | (1u << capture->sm) |
            (1u << responder->sm));
        service_bus(capture, responder, result,
                    time_us_64() + timeout_us_from_clocks(RUN_TIMEOUT_CLOCKS));
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
        restore_interrupts(irq_state);
    } else {
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
    }

    pio_sm_set_enabled(capture->pio, capture->sm, false);
    pio_sm_set_enabled(phase->pio, phase->sm, false);
    pio_sm_set_enabled(observer->pio, observer->sm, false);
    pio_sm_set_enabled(responder->pio, responder->sm, false);
    pio_sm_exec(responder->pio, responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));

    while (!pio_sm_is_rx_fifo_empty(phase->pio, phase->sm) &&
           result->phase_count < FIRST_PHASE_COUNT) {
        result->phase_raw[result->phase_count++] =
            pio_sm_get(phase->pio, phase->sm);
    }

    for (uint spin = 0u;
         spin < 1024u &&
         !pio_sm_is_rx_fifo_empty(observer->pio, observer->sm);
         ++spin)
        tight_loop_contents();
    result->observer_fifo_residue =
        pio_sm_get_rx_fifo_level(observer->pio, observer->sm);
    result->observer_dma_post = dma_remaining(observer_dma);
    dma_channel_abort((uint)observer_dma);
    result->observer_count = TRACE_DEPTH - result->observer_dma_post;
    for (uint i = 0u; i < result->observer_count; ++i)
        result->observer_raw[i] = g_observer_dma_words[i];
    result->dma_observer_first_ok = result->observer_count > 0u &&
        decode_address(result->observer_raw[0]) == 0xFFFF0u;
    dma_channel_unclaim((uint)observer_dma);

    route_ad_to_sio_high_z();
    gpio_put(V30_PIN_INTR, false);
    result->terminal_safe = gpio_get(V30_PIN_RESET) &&
                            !gpio_get(V30_PIN_CLK) &&
                            ad_is_sio_high_z();
    result->first_address_ok = result->trace_count > 0u &&
                              result->trace[0].address == 0xFFFF0u;
    result->first_memory_read = result->trace_count > 0u &&
                                result->trace[0].memory_read;
    if (result->phase_count == FIRST_PHASE_COUNT) {
        result->first_response_phase_ok =
            decode_ad(result->phase_raw[3]) == 0x00EAu &&
            decode_ad(result->phase_raw[4]) == 0x00EAu &&
            decode_ad(result->phase_raw[5]) == 0x00EAu;
        if (!result->first_response_phase_ok) ++result->deadline_misses;
    }
}

static bool result_valid(const pc1c0b_result_t *result) {
    return result->reset_ok && result->pre_release_clean &&
           result->first_address_ok && result->first_memory_read &&
           result->dma_observer_first_ok &&
           result->observer_fifo_residue == 0u &&
           result->capture_phase_errors == 0u &&
           result->phase_count == FIRST_PHASE_COUNT;
}

static bool result_pass(const pc1c0b_result_t *result) {
    return result_valid(result) && result->first_response_phase_ok &&
           result->required_hit_mask == 0x07u &&
           result->far_target_seen && result->deadline_misses == 0u &&
           result->late_response_commands == 0u &&
           result->tx_backpressure == 0u &&
           result->unqualified_drive_commands == 0u && result->terminal_safe;
}

static const char *lane_name(lane_mask_t lanes) {
    switch (lanes) {
        case LANE_LOW: return "LOW";
        case LANE_HIGH: return "HIGH";
        case LANES_WORD: return "WORD";
        default: return "NONE";
    }
}

static void print_result(const pc1c0b_result_t *result) {
    printf("\nPC1-C0B Qualified Reset-Vector Response - 0.300 MHz\n");
    printf("RESET qualification : clock-only; response/capture SMs disabled\n");
    printf("Measurement epoch   : arm after RESET clocks with CLK stopped LOW\n");
    printf("Realtime engine     : PIO1 synchronized CLK + T1 capture + AD/PINDIRS\n");
    printf("Response policy     : M33 current-cycle SRAM lookup -> PIO1 TX FIFO\n");
    printf("Observer path       : passive PIO0 -> DMA -> SRAM trace\n");
    printf("ROM bytes           : EA 00 00 00 F0 90 at FFFF0\n");
    printf("Input synchronizers : SDK defaults\n\n");
    printf("RESET clock count         = %s\n", result->reset_ok ? "PASS" : "FAIL");
    printf("PRE-RESET EVENT LEAK      = %s\n",
           result->pre_release_clean ? "NO" : "YES / INVALID");
    printf("FIRST post-reset address  = %s",
           result->first_address_ok ? "FFFF0 PASS" : "FAIL");
    if (result->trace_count > 0u)
        printf(" (observed %05lX)", (unsigned long)result->trace[0].address);
    printf("\n");
    printf("FIRST cycle type          = %s\n",
           result->first_memory_read ? "MEMORY READ PASS" : "FAIL");
    printf("T1 capture phase errors   = %lu %s\n",
           (unsigned long)result->capture_phase_errors,
           result->capture_phase_errors == 0u ? "PASS" : "FAIL");
    printf("DMA observer first address= %s\n",
           result->dma_observer_first_ok ? "FFFF0 PASS" : "FAIL");
    printf("DMA observer words        = %u (remain %lu/%lu)\n",
           result->observer_count,
           (unsigned long)result->observer_dma_post,
           (unsigned long)result->observer_dma_pre);
    printf("DMA observer FIFO residue = %lu %s\n",
           (unsigned long)result->observer_fifo_residue,
           result->observer_fifo_residue == 0u ? "PASS" : "FAIL");
    printf("FIRST response at R2/F2/R3= %s\n",
           result->first_response_phase_ok ? "00EA PASS" : "FAIL");
    printf("Required ROM hit mask     = %02X %s\n", result->required_hit_mask,
           result->required_hit_mask == 0x07u ? "PASS" : "FAIL");
    printf("Qualified ROM responses   = %lu\n", (unsigned long)result->rom_hits);
    printf("Unsupported/high-Z cycles = %lu\n",
           (unsigned long)result->unsupported_cycles);
    printf("Response deadline misses  = %lu %s\n",
           (unsigned long)result->deadline_misses,
           result->deadline_misses == 0u ? "PASS" : "FAIL");
    printf("Commands late at ASTB fall= %lu %s\n",
           (unsigned long)result->late_response_commands,
           result->late_response_commands == 0u ? "PASS" : "FAIL");
    printf("TX FIFO backpressure      = %lu %s\n",
           (unsigned long)result->tx_backpressure,
           result->tx_backpressure == 0u ? "PASS" : "FAIL");
    printf("Unqualified drive commands= %lu %s\n",
           (unsigned long)result->unqualified_drive_commands,
           result->unqualified_drive_commands == 0u ? "PASS" : "FAIL");
    printf("Far-jump target observed  = %s\n",
           result->far_target_seen ? "F0000 PASS" : "FAIL");

    printf("\n[QUALIFIED BUS TRACE]\n");
    printf("idx address type lanes hit response raw_t1\n");
    for (uint i = 0u; i < result->trace_count; ++i) {
        const trace_entry_t *entry = &result->trace[i];
        printf("%02u  %05lX  %-5s %-4s  %s  %04X     %08lX\n",
               i, (unsigned long)entry->address,
               entry->memory_read ? "MEMR" : "OTHER",
               lane_name(entry->lanes), entry->rom_hit ? "YES" : "NO ",
               entry->response,
               (unsigned long)entry->cycle_raw);
    }

    printf("\n[DMA PASSIVE ADDRESS TRACE]\n");
    for (uint i = 0u; i < result->observer_count; ++i)
        printf("%02u = %05lX raw=%08lX\n", i,
               (unsigned long)decode_address(result->observer_raw[i]),
               (unsigned long)result->observer_raw[i]);

    static const char *const phase_names[FIRST_PHASE_COUNT] = {
        "AF", "R1", "F1", "R2", "F2", "R3"
    };
    printf("\n[FIRST-CYCLE GPIO SNAPSHOTS]\n");
    printf("phase raw_gpio  ASTB CLK IOM BUFRW INTAK UBE AD16\n");
    for (uint i = 0u; i < result->phase_count; ++i) {
        const uint32_t raw = result->phase_raw[i];
        printf("%-4s  %08lX   %u   %u   %u   %u    %u   %u  %04X\n",
               phase_names[i], (unsigned long)raw,
               (unsigned)sample_bit(raw, V30_PIN_ASTB),
               (unsigned)sample_bit(raw, V30_PIN_CLK),
               (unsigned)sample_bit(raw, V30_PIN_IOM),
               (unsigned)sample_bit(raw, V30_PIN_BUFRW),
               (unsigned)sample_bit(raw, V30_PIN_INTAK),
               (unsigned)sample_bit(raw, V30_PIN_UBE),
               (unsigned)decode_ad(raw));
    }

    printf("\nMEASUREMENT EPOCH  = %s\n",
           result_valid(result) ? "VALID" : "INVALID");
    printf("PC1-C0B RESULT     = %s\n",
           result_pass(result) ? "PASS" :
           (result_valid(result) ? "FAIL" : "INVALID"));
    printf("TERMINAL SAFE STATE= %s\n", result->terminal_safe ? "PASS" : "FAIL");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    static const uint8_t image[RESET_ROM_SIZE] = {
        0xEAu, 0x00u, 0x00u, 0x00u, 0xF0u, 0x90u,
    };
    for (uint i = 0u; i < RESET_ROM_SIZE; ++i) g_reset_rom[i] = image[i];

    prepare_header_high_z();
    init_control_outputs();
    route_ad_to_sio_high_z();
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    pc1c_sm_t clock, capture, phase, observer, responder;
    clock_init(&clock);
    bus_capture_init(&capture);
    phase_capture_init(&phase);
    observer_init(&observer);
    responder_init(&responder);

    pc1c0b_result_t result;
    run_test(&clock, &capture, &phase, &observer, &responder, &result);
    print_result(&result);
    fflush(stdout);
    while (true) tight_loop_contents();
}
