/*
 * Issue #106 Phase C2+C3 integration laboratory.
 *
 * Proves one bounded physical dataplane:
 *
 *   physical ASTB/T1 snapshot
 *       -> scattered A15..A0 extraction
 *       -> hardware A0 + UBE/BHE# lane classification
 *       -> 64 KiB-aligned internal-SRAM pointer
 *       -> DMA AL3_READ_ADDR_TRIG
 *       -> autonomous 16-bit SRAM read
 *       -> evidence buffer
 *
 * An independent PIO observer records raw GPIO snapshots and canonical lane
 * codes for post-run cross-checking. M33 prepares the finite experiment and
 * verifies it afterwards; it does not service active bus cycles.
 *
 * This lab intentionally uses a 64 KiB mirrored backing window:
 *
 *   pointer = backing[31:16] : physical_address[15:0]
 *
 * That isolates physical frontend + lane + DMA/SRAM feasibility from the final
 * 20-bit memory-map translation policy. Only WORD lanes are serviced here.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bus/processor_bus_pins.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/dma.h"
#include "issue106_c23_integration.pio.h"
#include "free_running_clock.pio.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#define ISSUE106_VECTOR_COUNT 16u
#define ISSUE106_ADDRESS_BITS 16u
#define ISSUE106_TX_WORDS_PER_CYCLE (1u + ISSUE106_ADDRESS_BITS + 1u)
#define ISSUE106_TX_WORD_COUNT \
    (ISSUE106_VECTOR_COUNT * ISSUE106_TX_WORDS_PER_CYCLE)
#define ISSUE106_OBSERVER_WORD_COUNT (ISSUE106_VECTOR_COUNT * 2u)
#define ISSUE106_PROCESSOR_HZ 1000000u
#define ISSUE106_TIMEOUT_US 100000u
#define ISSUE106_FINISH_POINTER_REL 16u

#define ISSUE106_LANE_INVALID 0u
#define ISSUE106_LANE_LOW     1u
#define ISSUE106_LANE_HIGH    2u
#define ISSUE106_LANE_WORD    3u

/*
 * 64 KiB alignment is the translation primitive: the PIO only has to append
 * A15..A0 to the fixed SRAM high half to form a legal absolute pointer.
 */
static uint16_t backing[32768] __attribute__((aligned(65536)));
static uint32_t tx_words[ISSUE106_TX_WORD_COUNT];
static uint32_t observer_words[ISSUE106_OBSERVER_WORD_COUNT];
static uint16_t serviced_data[ISSUE106_VECTOR_COUNT];

static const uint8_t low_address_gpio[ISSUE106_ADDRESS_BITS] = {
    RP86_PROCESSOR_PIN_AD0,
    RP86_PROCESSOR_PIN_AD1,
    RP86_PROCESSOR_PIN_AD2,
    RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4,
    RP86_PROCESSOR_PIN_AD5,
    RP86_PROCESSOR_PIN_AD6,
    RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8,
    RP86_PROCESSOR_PIN_AD9,
    RP86_PROCESSOR_PIN_AD10,
    RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12,
    RP86_PROCESSOR_PIN_AD13,
    RP86_PROCESSOR_PIN_AD14,
    RP86_PROCESSOR_PIN_AD15,
};

static const uint8_t full_address_gpio[20] = {
    RP86_PROCESSOR_PIN_AD0,
    RP86_PROCESSOR_PIN_AD1,
    RP86_PROCESSOR_PIN_AD2,
    RP86_PROCESSOR_PIN_AD3,
    RP86_PROCESSOR_PIN_AD4,
    RP86_PROCESSOR_PIN_AD5,
    RP86_PROCESSOR_PIN_AD6,
    RP86_PROCESSOR_PIN_AD7,
    RP86_PROCESSOR_PIN_AD8,
    RP86_PROCESSOR_PIN_AD9,
    RP86_PROCESSOR_PIN_AD10,
    RP86_PROCESSOR_PIN_AD11,
    RP86_PROCESSOR_PIN_AD12,
    RP86_PROCESSOR_PIN_AD13,
    RP86_PROCESSOR_PIN_AD14,
    RP86_PROCESSOR_PIN_AD15,
    RP86_PROCESSOR_PIN_A16,
    RP86_PROCESSOR_PIN_A17,
    RP86_PROCESSOR_PIN_A18,
    RP86_PROCESSOR_PIN_A19,
};

static inline uint32_t sample_bit(uint32_t raw, uint gpio) {
    return (raw >> gpio) & 1u;
}

static uint32_t decode_raw_address(uint32_t raw) {
    uint32_t address = 0u;
    for (uint bit = 0u; bit < 20u; ++bit)
        address |= sample_bit(raw, full_address_gpio[bit]) << bit;
    return address & 0xfffffu;
}

static uint32_t classify_lane_software(uint32_t raw) {
    const uint32_t a0 = sample_bit(raw, RP86_PROCESSOR_PIN_AD0);
    const uint32_t bhe_n = sample_bit(raw, RP86_PROCESSOR_PIN_UBE);

    if (a0 == 0u && bhe_n == 0u) return ISSUE106_LANE_WORD;
    if (a0 == 0u && bhe_n == 1u) return ISSUE106_LANE_LOW;
    if (a0 == 1u && bhe_n == 0u) return ISSUE106_LANE_HIGH;
    return ISSUE106_LANE_INVALID;
}

static const char *lane_name(uint32_t lane) {
    switch (lane) {
        case ISSUE106_LANE_LOW: return "LOW";
        case ISSUE106_LANE_HIGH: return "HIGH";
        case ISSUE106_LANE_WORD: return "WORD";
        default: return "INVALID";
    }
}

static uint16_t extraction_instruction(uint gpio) {
    return gpio == 0u ? pio_encode_nop() : pio_encode_out(pio_null, gpio);
}

static void build_tx_stream(uint program_offset) {
    const uint32_t base_hi =
        ((uint32_t)(uintptr_t)backing >> 16) & 0xffffu;
    const uint16_t finish_jump =
        pio_encode_jmp(program_offset + ISSUE106_FINISH_POINTER_REL);

    uint cursor = 0u;
    for (uint cycle = 0u; cycle < ISSUE106_VECTOR_COUNT; ++cycle) {
        tx_words[cursor++] = base_hi;
        for (int bit = ISSUE106_ADDRESS_BITS - 1; bit >= 0; --bit)
            tx_words[cursor++] =
                extraction_instruction(low_address_gpio[(uint)bit]);
        tx_words[cursor++] = finish_jump;
    }
    hard_assert(cursor == ISSUE106_TX_WORD_COUNT);
}

static void prepare_header_safe(void) {
    for (uint gpio = 0u; gpio <= 27u; ++gpio) {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_disable_pulls(gpio);
    }

    gpio_init(RP86_PROCESSOR_PIN_RESET);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_RESET);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    gpio_set_dir(RP86_PROCESSOR_PIN_RESET, GPIO_OUT);

    gpio_init(RP86_PROCESSOR_PIN_INTR);
    gpio_disable_pulls(RP86_PROCESSOR_PIN_INTR);
    gpio_put(RP86_PROCESSOR_PIN_INTR, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_INTR, GPIO_OUT);
}

static void init_service_frontend(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        issue106_c23_service_frontend_program_get_default_config(offset);

    sm_config_set_in_pins(&config, 0u);
    sm_config_set_out_shift(&config, true, false, 32u);
    sm_config_set_in_shift(&config, false, false, 32u);
    sm_config_set_jmp_pin(&config, RP86_PROCESSOR_PIN_UBE);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);

    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_lane_observer(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        issue106_c23_lane_observer_program_get_default_config(offset);

    sm_config_set_in_pins(&config, 0u);
    sm_config_set_out_shift(&config, true, false, 32u);
    sm_config_set_jmp_pin(&config, RP86_PROCESSOR_PIN_UBE);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);

    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_set_enabled(pio, sm, true);
}

static void init_clock_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config config =
        rp86_free_running_clock_program_get_default_config(offset);
    const float divider =
        (float)clock_get_hz(clk_sys) / (2.0f * (float)ISSUE106_PROCESSOR_HZ);

    sm_config_set_set_pins(&config, RP86_PROCESSOR_PIN_CLK, 1u);
    sm_config_set_clkdiv(&config, divider);

    pio_gpio_init(pio, RP86_PROCESSOR_PIN_CLK);
    pio_sm_set_consecutive_pindirs(
        pio, sm, RP86_PROCESSOR_PIN_CLK, 1u, true);
    hard_assert(pio_sm_init(pio, sm, offset, &config) == PICO_OK);
    pio_sm_set_enabled(pio, sm, true);
}

static bool wait_reset_clocks(uint count) {
    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    bool last = gpio_get(RP86_PROCESSOR_PIN_CLK);
    uint falling_edges = 0u;

    while (falling_edges < count && time_us_64() <= deadline) {
        const bool now = gpio_get(RP86_PROCESSOR_PIN_CLK);
        if (last && !now) ++falling_edges;
        last = now;
        tight_loop_contents();
    }
    return falling_edges == count;
}

static bool wait_dma_idle(uint channel, uint64_t deadline) {
    while (dma_channel_is_busy(channel)) {
        if (time_us_64() > deadline) return false;
        tight_loop_contents();
    }
    return true;
}

int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(50);

    printf("issue106 C2+C3 physical lane + SRAM integration lab\n");
    printf("scope: T1 pads -> lane gate -> 64KiB mirrored SRAM pointer -> DMA read\n");
    printf("processor clock: %u Hz; response bus drive/timing is NOT claimed\n",
           ISSUE106_PROCESSOR_HZ);

    const uintptr_t backing_addr = (uintptr_t)backing;
    hard_assert((backing_addr & 0xffffu) == 0u);

    for (uint i = 0u; i < 32768u; ++i)
        backing[i] = (uint16_t)(0xa500u ^ i ^ (i << 3));
    for (uint i = 0u; i < ISSUE106_VECTOR_COUNT; ++i)
        serviced_data[i] = 0u;

    prepare_header_safe();

    /* PIO0: physical service frontend. */
    PIO service_pio = pio0;
    const uint service_sm = pio_claim_unused_sm(service_pio, true);
    const uint service_offset =
        pio_add_program(service_pio, &issue106_c23_service_frontend_program);
    init_service_frontend(service_pio, service_sm, service_offset);
    build_tx_stream(service_offset);

    /* Feed the fixed per-cycle base/microcode stream. */
    const uint tx_dma = dma_claim_unused_channel(true);
    dma_channel_config tx_cfg = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&tx_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_cfg, true);
    channel_config_set_write_increment(&tx_cfg, false);
    channel_config_set_dreq(
        &tx_cfg, pio_get_dreq(service_pio, service_sm, true));
    channel_config_set_high_priority(&tx_cfg, true);
    dma_channel_configure(tx_dma, &tx_cfg,
                          &service_pio->txf[service_sm],
                          tx_words, ISSUE106_TX_WORD_COUNT, false);

    /* One-word dynamic SRAM read channel. READ_ADDR is replaced per WORD cycle. */
    const uint data_dma = dma_claim_unused_channel(true);
    dma_channel_config data_cfg = dma_channel_get_default_config(data_dma);
    channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&data_cfg, false);
    channel_config_set_write_increment(&data_cfg, true);
    channel_config_set_dreq(&data_cfg, DREQ_FORCE);
    channel_config_set_high_priority(&data_cfg, true);
    dma_channel_configure(data_dma, &data_cfg,
                          serviced_data, backing,
                          1u, false);

    /* Service PIO RX publishes only WORD-lane absolute pointers. */
    const uint control_dma = dma_claim_unused_channel(true);
    dma_channel_config control_cfg = dma_channel_get_default_config(control_dma);
    channel_config_set_transfer_data_size(&control_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&control_cfg, false);
    channel_config_set_write_increment(&control_cfg, false);
    channel_config_set_dreq(
        &control_cfg, pio_get_dreq(service_pio, service_sm, false));
    channel_config_set_high_priority(&control_cfg, true);
    dma_channel_configure(
        control_dma, &control_cfg,
        &dma_channel_hw_addr(data_dma)->al3_read_addr_trig,
        &service_pio->rxf[service_sm], ISSUE106_VECTOR_COUNT, false);

    /*
     * PIO1: independent raw/lane observer. This is evidence only and does not
     * feed the service path.
     */
    PIO observer_pio = pio1;
    const uint observer_sm = pio_claim_unused_sm(observer_pio, true);
    const uint observer_offset =
        pio_add_program(observer_pio, &issue106_c23_lane_observer_program);
    init_lane_observer(observer_pio, observer_sm, observer_offset);

    const uint observer_dma = dma_claim_unused_channel(true);
    dma_channel_config observer_cfg =
        dma_channel_get_default_config(observer_dma);
    channel_config_set_transfer_data_size(&observer_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&observer_cfg, false);
    channel_config_set_write_increment(&observer_cfg, true);
    channel_config_set_dreq(
        &observer_cfg, pio_get_dreq(observer_pio, observer_sm, false));
    channel_config_set_high_priority(&observer_cfg, true);
    dma_channel_configure(observer_dma, &observer_cfg,
                          observer_words, &observer_pio->rxf[observer_sm],
                          ISSUE106_OBSERVER_WORD_COUNT, false);

    dma_start_channel_mask(
        (1u << tx_dma) | (1u << control_dma) | (1u << observer_dma));

    /* PIO1 also owns the free-running processor clock. */
    const uint clock_sm = pio_claim_unused_sm(observer_pio, true);
    const uint clock_offset =
        pio_add_program(observer_pio, &rp86_free_running_clock_program);
    init_clock_sm(observer_pio, clock_sm, clock_offset);

    const bool reset_clock_ok = wait_reset_clocks(8u);
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);

    const uint64_t deadline = time_us_64() + ISSUE106_TIMEOUT_US;
    const bool observer_complete =
        wait_dma_idle(observer_dma, deadline);
    const bool frontend_complete =
        wait_dma_idle(tx_dma, deadline);

    /* End the finite physical epoch before post-run inspection. */
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    (void)wait_reset_clocks(8u);
    sleep_us(100u);

    pio_sm_set_enabled(observer_pio, clock_sm, false);
    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);

    const uint32_t control_remaining =
        dma_channel_hw_addr(control_dma)->transfer_count & 0x0fffffffu;
    const uint32_t service_count =
        ISSUE106_VECTOR_COUNT - control_remaining;

    const uintptr_t data_write =
        (uintptr_t)dma_channel_hw_addr(data_dma)->write_addr;
    uint32_t data_count = 0u;
    if (data_write >= (uintptr_t)serviced_data) {
        data_count = (uint32_t)(
            (data_write - (uintptr_t)serviced_data) / sizeof(uint16_t));
        if (data_count > ISSUE106_VECTOR_COUNT)
            data_count = ISSUE106_VECTOR_COUNT;
    }

    uint lane_failures = 0u;
    uint word_cycles = 0u;
    uint service_failures = 0u;
    uint service_index = 0u;
    bool first_reset_vector_ok = false;

    for (uint i = 0u; i < ISSUE106_VECTOR_COUNT; ++i) {
        const uint32_t raw = observer_words[i * 2u];
        const uint32_t address = decode_raw_address(raw);
        const uint32_t expected_lane = classify_lane_software(raw);
        const uint32_t observed_lane = observer_words[i * 2u + 1u] & 3u;
        const bool lane_pass = expected_lane == observed_lane;

        if (i == 0u) first_reset_vector_ok = address == 0xffff0u;
        if (!lane_pass) ++lane_failures;

        printf("[%02u] raw=%08" PRIx32 " addr=%05" PRIx32
               " lane=%s/%s %s",
               i, raw, address,
               lane_name(expected_lane), lane_name(observed_lane),
               lane_pass ? "PASS" : "FAIL");

        if (expected_lane == ISSUE106_LANE_WORD) {
            ++word_cycles;
            const uint16_t expected_data =
                backing[(address & 0xffffu) >> 1u];
            if (service_index < data_count) {
                const uint16_t actual = serviced_data[service_index];
                const bool data_pass = actual == expected_data;
                printf(" sram[%04" PRIx32 "]=%04x/%04x %s",
                       address & 0xffffu, expected_data, actual,
                       data_pass ? "PASS" : "FAIL");
                if (!data_pass) ++service_failures;
            } else {
                printf(" sram[%04" PRIx32 "]=%04x/MISSING FAIL",
                       address & 0xffffu, expected_data);
                ++service_failures;
            }
            ++service_index;
        }

        printf("\n");
    }

    if (service_count != word_cycles) ++service_failures;
    if (data_count != word_cycles) ++service_failures;

    const bool pass =
        reset_clock_ok &&
        observer_complete &&
        frontend_complete &&
        first_reset_vector_ok &&
        lane_failures == 0u &&
        service_failures == 0u;

    printf("BACKING BASE: %08" PRIxPTR " (64KiB aligned)\n", backing_addr);
    printf("RESET CLOCKS: %s\n", reset_clock_ok ? "PASS" : "FAIL");
    printf("FIRST RESET VECTOR FFFF0: %s\n",
           first_reset_vector_ok ? "PASS" : "FAIL");
    printf("LANE CLASSIFICATION: %s (%u mismatches)\n",
           lane_failures == 0u ? "PASS" : "FAIL", lane_failures);
    printf("WORD SERVICE: expected=%u pointer_triggers=%u data_reads=%u\n",
           word_cycles, service_count, data_count);
    printf("RESULT: %s (service failures=%u)\n",
           pass ? "PASS" : "FAIL", service_failures);
    printf("NOTE: 64KiB mirrored translation + WORD reads only; full 20-bit ");
    printf("mapping, byte lanes, AD-bus response, and Intel 2MHz timing remain pending.\n");

    while (true) tight_loop_contents();
}
