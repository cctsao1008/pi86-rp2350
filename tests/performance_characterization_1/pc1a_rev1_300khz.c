#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "memory/memory.h"
#include "pic/pic.h"
#include "pit/pit.h"
#include "v30/v30_bus.h"
#include "v30/v30_pins.h"
#include "perf_continuous_clock.pio.h"

#define PI86_ROM_BASE              0xF0000u
#define ROM_SIZE                   0x10000u
#define RAM_BASE                   0x00000u
#define RAM_SIZE                   0x10000u
#define RESET_VECTOR_ADDR          0xFFFF0u
#define VECTOR_BASE                0x20u
#define IRQ0_VECTOR                0x20u
#define IRQ0_LINE                  0u
#define IRQ0_IVT_ENTRY_ADDR        (IRQ0_VECTOR * 4u)
#define IRQ0_ISR_ADDR              0xF0100u
#define IRQ0_ISR_OFFSET            0x0100u
#define ISR_SEGMENT                0xF000u
#define IRQ0_MARKER_ADDR           0x00300u
#define IRQ0_MARKER_VALUE          0xA0u
#define SUCCESS_LOOP_ADDR          0xF0033u

#define REV1_V30_HZ                300000u
#define RESET_CLOCKS               20u
#define SIGNAL_TIMEOUT_CLOCKS      64u
#define MAX_BUS_CYCLES             360u
#define SUCCESS_HITS_REQUIRED      3u
#define MIN_ALE_HIGH_SAMPLES_REQ   3u
#define TRACE_DEPTH                24u

static uint8_t rom[ROM_SIZE];
static uint8_t ram[RAM_SIZE];

static const uint8_t ad_gpio[16] = {
    V30_PIN_AD0, V30_PIN_AD1, V30_PIN_AD2, V30_PIN_AD3,
    V30_PIN_AD4, V30_PIN_AD5, V30_PIN_AD6, V30_PIN_AD7,
    V30_PIN_AD8, V30_PIN_AD9, V30_PIN_AD10, V30_PIN_AD11,
    V30_PIN_AD12, V30_PIN_AD13, V30_PIN_AD14, V30_PIN_AD15,
};

static uint32_t data_lo_lut[256];
static uint32_t data_hi_lut[256];
static uint32_t low_lane_mask_value;
static uint32_t high_lane_mask_value;

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} perf_clock_t;

typedef enum {
    REV1_FAIL_NONE = 0,
    REV1_FAIL_RESET_CLOCK_TIMEOUT,
    REV1_FAIL_ALE_LOW_TIMEOUT,
    REV1_FAIL_ALE_HIGH_TIMEOUT,
    REV1_FAIL_CONTROL_TIMEOUT,
    REV1_FAIL_CYCLE0_ASSERT,
    REV1_FAIL_BUS_CYCLE,
    REV1_FAIL_MEMORY,
    REV1_FAIL_IO,
    REV1_FAIL_INTA,
    REV1_FAIL_TERMINAL_COUNT,
    REV1_FAIL_SUCCESS_TIMEOUT,
    REV1_FAIL_CAPTURE_MARGIN,
} rev1_fail_reason_t;

typedef struct {
    uint32_t sequence;
    uint32_t t1_raw;
    uint32_t control_raw;
    uint32_t address;
    uint16_t control_ad;
    uint16_t ale_high_samples;
    uint8_t high_nibble;
    uint8_t cycle_type;
    uint8_t lanes;
} trace_entry_t;

typedef struct {
    trace_entry_t entries[TRACE_DEPTH];
    uint32_t write_count;
} trace_ring_t;

typedef struct {
    bool pass;
    rev1_fail_reason_t fail_reason;
    uint cycles;
    uint success_hits;
    bool cycle0_ok;
    bool first_read_ok;
    bool pit_programmed;
    bool no_irq_before_terminal;
    bool terminal_count;
    bool irq0_routed;
    uint inta_total;
    bool vector20;
    bool ivt_offset;
    bool ivt_segment;
    bool isr_fetch;
    bool marker;
    bool eoi;
    uint stack_7ffa_writes;
    uint stack_7ffc_writes;
    uint stack_7ffe_writes;
    uint stack_7ffa_reads;
    uint stack_7ffc_reads;
    uint stack_7ffe_reads;
    uint min_ale_high_samples;
    uint32_t first_fault_sequence;
    bool first_fault_valid;
    uint32_t first_fault_address;
    uint8_t first_fault_type;
    uint8_t first_fault_lanes;
    uint8_t final_irr;
    uint8_t final_isr;
    bool final_intr;
} rev1_result_t;

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static const char *fail_reason_name(rev1_fail_reason_t reason) {
    switch (reason) {
        case REV1_FAIL_NONE: return "none";
        case REV1_FAIL_RESET_CLOCK_TIMEOUT: return "RESET clock-count timeout";
        case REV1_FAIL_ALE_LOW_TIMEOUT: return "ALE-low anchor timeout";
        case REV1_FAIL_ALE_HIGH_TIMEOUT: return "ALE-high timeout";
        case REV1_FAIL_CONTROL_TIMEOUT: return "control-phase timeout";
        case REV1_FAIL_CYCLE0_ASSERT: return "cycle-zero assertion failed";
        case REV1_FAIL_BUS_CYCLE: return "unsupported/corrupted bus cycle";
        case REV1_FAIL_MEMORY: return "memory transaction failure";
        case REV1_FAIL_IO: return "I/O transaction failure";
        case REV1_FAIL_INTA: return "interrupt acknowledge failure";
        case REV1_FAIL_TERMINAL_COUNT: return "PIT terminal-count routing failure";
        case REV1_FAIL_SUCCESS_TIMEOUT: return "SUCCESS not reached before cycle limit";
        case REV1_FAIL_CAPTURE_MARGIN: return "ALE-high capture margin below requirement";
        default: return "unknown";
    }
}

static void init_data_path(void) {
    low_lane_mask_value = 0u;
    high_lane_mask_value = 0u;
    for (uint bit = 0u; bit < 8u; ++bit) {
        low_lane_mask_value |= 1u << ad_gpio[bit];
        high_lane_mask_value |= 1u << ad_gpio[bit + 8u];
    }
    for (uint32_t value = 0u; value < 256u; ++value) {
        uint32_t lo = 0u;
        uint32_t hi = 0u;
        for (uint bit = 0u; bit < 8u; ++bit) {
            if ((value >> bit) & 1u) {
                lo |= 1u << ad_gpio[bit];
                hi |= 1u << ad_gpio[bit + 8u];
            }
        }
        data_lo_lut[value] = lo;
        data_hi_lut[value] = hi;
    }
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

static inline void hold_reset(bool asserted) {
    gpio_put(V30_PIN_RESET, asserted);
}

static inline void set_intr(bool asserted) {
    gpio_put(V30_PIN_INTR, asserted);
}

static inline void release_ad(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static inline void drive_data(uint16_t value, v30_bus_lanes_t lanes) {
    uint32_t encoded = 0u;
    uint32_t oe_mask = 0u;
    if ((lanes & V30_BUS_LANE_LOW) != 0u) {
        encoded |= data_lo_lut[value & 0xFFu];
        oe_mask |= low_lane_mask_value;
    }
    if ((lanes & V30_BUS_LANE_HIGH) != 0u) {
        encoded |= data_hi_lut[(value >> 8) & 0xFFu];
        oe_mask |= high_lane_mask_value;
    }
    sio_hw->gpio_clr = V30_AD_BUS_MASK;
    sio_hw->gpio_set = encoded;
    sio_hw->gpio_oe_set = oe_mask;
}

static inline uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0u; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
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

static inline v30_bus_lanes_t decode_lanes(uint8_t a0, uint8_t bhe_n) {
    if (a0 == 0u && bhe_n == 0u) return V30_BUS_LANES_WORD;
    if (a0 == 0u && bhe_n == 1u) return V30_BUS_LANE_LOW;
    if (a0 == 1u && bhe_n == 0u) return V30_BUS_LANE_HIGH;
    return V30_BUS_LANES_NONE;
}

static inline v30_bus_cycle_type_t decode_cycle_type(uint8_t iom,
                                                      uint8_t dtr,
                                                      uint8_t inta_n) {
    if (inta_n == 0u) return V30_BUS_CYCLE_INTERRUPT_ACK;
    if (iom != 0u)
        return dtr == 0u ? V30_BUS_CYCLE_MEM_READ : V30_BUS_CYCLE_MEM_WRITE;
    return dtr == 0u ? V30_BUS_CYCLE_IO_READ : V30_BUS_CYCLE_IO_WRITE;
}

static inline uint64_t timeout_us_from_clocks(uint32_t clocks) {
    uint64_t us = ((uint64_t)clocks * 1000000ull + REV1_V30_HZ - 1u) / REV1_V30_HZ;
    return us + 2u;
}

static bool __not_in_flash_func(wait_level_until)(uint gpio,
                                                   bool level,
                                                   uint64_t deadline_us,
                                                   uint32_t *sample_out) {
    while (time_us_64() <= deadline_us) {
        const uint32_t sample = sio_hw->gpio_in;
        if ((sample_bit(sample, gpio) != 0u) == level) {
            if (sample_out != NULL) *sample_out = sample;
            return true;
        }
    }
    return false;
}

static bool __not_in_flash_func(wait_falling_edge)(uint64_t timeout_us,
                                                    uint32_t *sample_out) {
    uint64_t deadline = time_us_64() + timeout_us;
    if (!wait_level_until(V30_PIN_CLK, true, deadline, NULL)) return false;
    deadline = time_us_64() + timeout_us;
    return wait_level_until(V30_PIN_CLK, false, deadline, sample_out);
}

static bool __not_in_flash_func(wait_falling_edges)(uint count) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    for (uint i = 0u; i < count; ++i) {
        if (!wait_falling_edge(timeout_us, NULL)) return false;
    }
    return true;
}

static bool __not_in_flash_func(wait_reset_clocks)(uint count) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    for (uint i = 0u; i < count; ++i) {
        if (!wait_falling_edge(timeout_us, NULL)) return false;
    }
    return true;
}

static void trace_push(trace_ring_t *ring,
                       uint32_t sequence,
                       const v30_bus_cycle_t *cycle,
                       uint16_t ale_samples) {
    trace_entry_t *e = &ring->entries[ring->write_count % TRACE_DEPTH];
    e->sequence = sequence;
    e->t1_raw = cycle->t1_sample;
    e->control_raw = cycle->control_sample;
    e->address = cycle->address;
    e->control_ad = decode_ad(cycle->control_sample);
    e->ale_high_samples = ale_samples;
    e->high_nibble = (uint8_t)((cycle->address >> 16) & 0x0Fu);
    e->cycle_type = (uint8_t)cycle->type;
    e->lanes = (uint8_t)cycle->lanes;
    ++ring->write_count;
}

static void remember_first_fault(rev1_result_t *result,
                                 uint32_t sequence,
                                 const v30_bus_cycle_t *cycle) {
    if (result->first_fault_valid) return;
    result->first_fault_valid = true;
    result->first_fault_sequence = sequence;
    result->first_fault_address = cycle->address;
    result->first_fault_type = (uint8_t)cycle->type;
    result->first_fault_lanes = (uint8_t)cycle->lanes;
}

/*
 * Software-transparent-latch T1 capture.
 *
 * Every cycle starts from a known ALE-low anchor.  Once ALE rises, keep the
 * latest coherent 32-bit SIO snapshot for as long as ALE remains high.  The
 * retained value is therefore the final coherent ALE-high sample immediately
 * before ALE falls, matching the external latch contract far more closely than
 * Rev0's first-ALE-high sample.
 */
static bool __not_in_flash_func(capture_cycle)(v30_bus_cycle_t *cycle,
                                                uint16_t *ale_high_samples,
                                                rev1_fail_reason_t *reason) {
    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    uint32_t sample = 0u;

    if (!wait_level_until(V30_PIN_ALE, false, time_us_64() + timeout_us, NULL)) {
        *reason = REV1_FAIL_ALE_LOW_TIMEOUT;
        return false;
    }
    if (!wait_level_until(V30_PIN_ALE, true, time_us_64() + timeout_us, &sample)) {
        *reason = REV1_FAIL_ALE_HIGH_TIMEOUT;
        return false;
    }

    uint32_t t1 = sample;
    uint16_t n = 0u;
    while (sample_bit(sample, V30_PIN_ALE) != 0u) {
        t1 = sample;
        if (n != UINT16_MAX) ++n;
        sample = sio_hw->gpio_in;
    }

    cycle->t1_sample = t1;
    cycle->address = decode_address(t1);
    cycle->a0 = (uint8_t)sample_bit(t1, V30_PIN_AD0);
    cycle->bhe_n = (uint8_t)sample_bit(t1, V30_PIN_BHE);
    cycle->lanes = decode_lanes(cycle->a0, cycle->bhe_n);

    /* Re-anchor control sampling from the corrected T1 end. */
    uint32_t control = 0u;
    if (!wait_falling_edge(timeout_us, &control)) {
        *reason = REV1_FAIL_CONTROL_TIMEOUT;
        return false;
    }

    cycle->control_sample = control;
    cycle->iom = (uint8_t)sample_bit(control, V30_PIN_IOM);
    cycle->dtr = (uint8_t)sample_bit(control, V30_PIN_DTR);
    cycle->inta_n = (uint8_t)sample_bit(control, V30_PIN_INTA);
    cycle->type = decode_cycle_type(cycle->iom, cycle->dtr, cycle->inta_n);
    cycle->idle_steps = 0u;

    *ale_high_samples = n;
    if (cycle->lanes == V30_BUS_LANES_NONE &&
        cycle->type != V30_BUS_CYCLE_INTERRUPT_ACK) {
        *reason = REV1_FAIL_BUS_CYCLE;
        return false;
    }
    return true;
}

static void perf_clock_init(perf_clock_t *clock, PIO pio) {
    clock->pio = pio;
    clock->sm = pio_claim_unused_sm(pio, true);
    clock->offset = pio_add_program(pio, &perf_continuous_clk_program);
}

static void perf_clock_start(perf_clock_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);

    pio_sm_config c = perf_continuous_clk_program_get_default_config(clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)REV1_V30_HZ));
    pio_gpio_init(clock->pio, V30_PIN_CLK);
    pio_sm_set_consecutive_pindirs(clock->pio, clock->sm, V30_PIN_CLK, 1u, true);
    pio_sm_init(clock->pio, clock->sm, clock->offset, &c);
    pio_sm_set_enabled(clock->pio, clock->sm, true);
}

static void perf_clock_stop(perf_clock_t *clock) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);
    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);
}

static void init_test_image(void) {
    for (uint32_t i = 0u; i < ROM_SIZE; ++i) rom[i] = 0x90u;
    for (uint32_t i = 0u; i < RAM_SIZE; ++i) ram[i] = 0x00u;

    static const uint8_t program[] = {
        0xFA,
        0xB8, 0x00, 0x00,
        0x8E, 0xD0,
        0xBC, 0x00, 0x80,
        0xB0, 0x11, 0xE6, 0x20,
        0xB0, 0x20, 0xE6, 0x21,
        0xB0, 0x00, 0xE6, 0x21,
        0xB0, 0x01, 0xE6, 0x21,
        0xB0, 0xFE, 0xE6, 0x21,
        0xB0, 0x30, 0xE6, 0x43,
        0xB0, 0x08, 0xE6, 0x40,
        0xB0, 0x00, 0xE6, 0x40,
        0xFB, 0x90, 0x90,
        0x80, 0x3E, 0x00, 0x03, 0xA0,
        0x75, 0xF9,
        0xEB, 0xFE,
    };
    for (uint32_t i = 0u; i < sizeof(program); ++i) rom[i] = program[i];

    static const uint8_t isr[] = {
        0xC6, 0x06, 0x00, 0x03, 0xA0,
        0xB0, 0x20,
        0xE6, 0x20,
        0xCF,
    };
    for (uint32_t i = 0u; i < sizeof(isr); ++i)
        rom[IRQ0_ISR_OFFSET + i] = isr[i];

    rom[0xFFF0] = 0xEAu;
    rom[0xFFF1] = 0x00u;
    rom[0xFFF2] = 0x00u;
    rom[0xFFF3] = 0x00u;
    rom[0xFFF4] = 0xF0u;
    rom[0xFFF5] = 0x90u;

    ram[IRQ0_IVT_ENTRY_ADDR + 0u] = (uint8_t)(IRQ0_ISR_OFFSET & 0xFFu);
    ram[IRQ0_IVT_ENTRY_ADDR + 1u] = (uint8_t)(IRQ0_ISR_OFFSET >> 8);
    ram[IRQ0_IVT_ENTRY_ADDR + 2u] = (uint8_t)(ISR_SEGMENT & 0xFFu);
    ram[IRQ0_IVT_ENTRY_ADDR + 3u] = (uint8_t)(ISR_SEGMENT >> 8);
}

static bool memory_read(const pi86_memory_t *memory,
                        const v30_bus_cycle_t *cycle,
                        uint16_t *value) {
    if (cycle->lanes == V30_BUS_LANES_WORD)
        return pi86_memory_read16(memory, cycle->address, value);
    uint8_t v = 0u;
    if (!pi86_memory_read8(memory, cycle->address, &v)) return false;
    if (cycle->lanes == V30_BUS_LANE_LOW) {
        *value = v;
        return true;
    }
    if (cycle->lanes == V30_BUS_LANE_HIGH) {
        *value = (uint16_t)v << 8;
        return true;
    }
    return false;
}

static bool memory_write(pi86_memory_t *memory,
                         const v30_bus_cycle_t *cycle,
                         uint16_t value) {
    if (cycle->lanes == V30_BUS_LANES_WORD)
        return pi86_memory_write16(memory, cycle->address, value);
    if (cycle->lanes == V30_BUS_LANE_LOW)
        return pi86_memory_write8(memory, cycle->address, (uint8_t)value);
    if (cycle->lanes == V30_BUS_LANE_HIGH)
        return pi86_memory_write8(memory, cycle->address, (uint8_t)(value >> 8));
    return false;
}

static bool extract_byte(const v30_bus_cycle_t *cycle,
                         uint16_t data,
                         uint8_t *value) {
    if (cycle->lanes == V30_BUS_LANE_LOW) {
        *value = (uint8_t)data;
        return true;
    }
    if (cycle->lanes == V30_BUS_LANE_HIGH) {
        *value = (uint8_t)(data >> 8);
        return true;
    }
    return false;
}

static bool service_inta(pi86_pic_t *pic, rev1_result_t *result) {
    bool drive_vector = false;
    uint8_t vector = 0u;
    const uint8_t phase_before = pi86_pic_acknowledge_phase(pic);
    if (!pi86_pic_begin_inta(pic, &drive_vector, &vector)) return false;
    ++result->inta_total;
    if (drive_vector) drive_data(vector, V30_BUS_LANE_LOW);
    else release_ad();

    const uint64_t timeout_us = timeout_us_from_clocks(SIGNAL_TIMEOUT_CLOCKS);
    if (!wait_level_until(V30_PIN_INTA, true, time_us_64() + timeout_us, NULL)) {
        release_ad();
        return false;
    }
    release_ad();
    if (!pi86_pic_end_inta(pic)) return false;
    if (phase_before == 1u && drive_vector && vector == IRQ0_VECTOR)
        result->vector20 = true;
    set_intr(pi86_pic_intr_asserted(pic));
    return true;
}

static bool service_io_write(pi86_pic_t *pic,
                             pi86_pit_t *pit,
                             const v30_bus_cycle_t *cycle,
                             rev1_result_t *result) {
    const uint16_t port = (uint16_t)(cycle->address & 0xFFFFu);
    const uint16_t raw = decode_ad(cycle->control_sample);
    uint8_t value = 0u;
    if (!extract_byte(cycle, raw, &value)) return false;

    if (port == PI86_PIC_COMMAND_PORT || port == PI86_PIC_DATA_PORT) {
        const uint8_t isr_before = pi86_pic_isr(pic);
        if (!pi86_pic_io_write8(pic, port, value)) return false;
        if (port == PI86_PIC_COMMAND_PORT && value == 0x20u &&
            (isr_before & 0x01u) != 0u && pi86_pic_isr(pic) == 0u)
            result->eoi = true;
        set_intr(pi86_pic_intr_asserted(pic));
        return true;
    }
    if (port == PI86_PIT_CONTROL_PORT || port == PI86_PIT_CHANNEL0_PORT) {
        if (!pi86_pit_io_write8(pit, port, value)) return false;
        if (pi86_pit_programmed(pit)) result->pit_programmed = true;
        return true;
    }
    return false;
}

static bool advance_pit(pi86_pit_t *pit,
                        pi86_pic_t *pic,
                        rev1_result_t *result) {
    if (!pi86_pit_programmed(pit) || !pi86_pit_counting(pit)) return true;
    if (pi86_pic_intr_asserted(pic)) result->no_irq_before_terminal = false;
    pi86_pit_tick(pit);
    if (!pi86_pit_take_terminal_count(pit)) return true;
    result->terminal_count = true;
    if (!pi86_pic_raise_irq(pic, IRQ0_LINE)) return false;
    result->irq0_routed = true;
    set_intr(pi86_pic_intr_asserted(pic));
    return true;
}

static bool __not_in_flash_func(run_point)(perf_clock_t *clock,
                                           rev1_result_t *result,
                                           trace_ring_t *trace) {
    *result = (rev1_result_t){0};
    *trace = (trace_ring_t){0};
    result->no_irq_before_terminal = true;
    result->min_ale_high_samples = UINT16_MAX;
    init_test_image();

    pi86_memory_t memory;
    pi86_memory_init(&memory, ram, RAM_BASE, RAM_SIZE, rom, PI86_ROM_BASE, ROM_SIZE);
    pi86_pic_t pic;
    pi86_pic_init(&pic, VECTOR_BASE);
    pi86_pit_t pit;
    pi86_pit_init(&pit);

    release_ad();
    set_intr(false);
    hold_reset(true);
    perf_clock_start(clock);

    if (!wait_reset_clocks(RESET_CLOCKS)) {
        result->fail_reason = REV1_FAIL_RESET_CLOCK_TIMEOUT;
        goto done;
    }

    /* RESET changes only after a counted falling edge; GPIO was initialized once. */
    hold_reset(false);

    while (result->cycles < MAX_BUS_CYCLES &&
           result->success_hits < SUCCESS_HITS_REQUIRED) {
        v30_bus_cycle_t cycle;
        uint16_t ale_samples = 0u;
        rev1_fail_reason_t capture_reason = REV1_FAIL_NONE;
        if (!capture_cycle(&cycle, &ale_samples, &capture_reason)) {
            result->fail_reason = capture_reason;
            break;
        }

        if (ale_samples < result->min_ale_high_samples)
            result->min_ale_high_samples = ale_samples;
        trace_push(trace, result->cycles, &cycle, ale_samples);

        if (result->cycles == 0u) {
            result->cycle0_ok = cycle.address == RESET_VECTOR_ADDR &&
                                cycle.type == V30_BUS_CYCLE_MEM_READ &&
                                cycle.lanes == V30_BUS_LANES_WORD;
            if (!result->cycle0_ok) {
                remember_first_fault(result, result->cycles, &cycle);
                result->fail_reason = REV1_FAIL_CYCLE0_ASSERT;
                break;
            }
        }

        if (cycle.type == V30_BUS_CYCLE_INTERRUPT_ACK) {
            if (!service_inta(&pic, result)) {
                remember_first_fault(result, result->cycles, &cycle);
                result->fail_reason = REV1_FAIL_INTA;
                break;
            }
            ++result->cycles;
            continue;
        }

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t driven = 0u;
            if (!memory_read(&memory, &cycle, &driven)) {
                remember_first_fault(result, result->cycles, &cycle);
                result->fail_reason = REV1_FAIL_MEMORY;
                break;
            }
            if (result->cycles == 0u) result->first_read_ok = true;

            drive_data(driven, cycle.lanes);
            if (!wait_falling_edges(2u)) {
                release_ad();
                remember_first_fault(result, result->cycles, &cycle);
                result->fail_reason = REV1_FAIL_CONTROL_TIMEOUT;
                break;
            }
            release_ad();

            if (cycle.address == IRQ0_IVT_ENTRY_ADDR && driven == IRQ0_ISR_OFFSET)
                result->ivt_offset = true;
            if (cycle.address == IRQ0_IVT_ENTRY_ADDR + 2u && driven == ISR_SEGMENT)
                result->ivt_segment = true;
            if (cycle.address == IRQ0_ISR_ADDR) result->isr_fetch = true;
            if (cycle.address == IRQ0_MARKER_ADDR &&
                (uint8_t)driven == IRQ0_MARKER_VALUE)
                result->marker = true;
            if (cycle.address == SUCCESS_LOOP_ADDR) ++result->success_hits;
            if (cycle.address == 0x07FFAu) ++result->stack_7ffa_reads;
            if (cycle.address == 0x07FFCu) ++result->stack_7ffc_reads;
            if (cycle.address == 0x07FFEu) ++result->stack_7ffe_reads;
        } else if (cycle.type == V30_BUS_CYCLE_MEM_WRITE) {
            const uint16_t data = decode_ad(cycle.control_sample);
            if (!memory_write(&memory, &cycle, data)) {
                remember_first_fault(result, result->cycles, &cycle);
                result->fail_reason = REV1_FAIL_MEMORY;
                break;
            }
            if (cycle.address == IRQ0_MARKER_ADDR &&
                cycle.lanes == V30_BUS_LANE_LOW &&
                (uint8_t)data == IRQ0_MARKER_VALUE)
                result->marker = true;
            if (cycle.address == 0x07FFAu) ++result->stack_7ffa_writes;
            if (cycle.address == 0x07FFCu) ++result->stack_7ffc_writes;
            if (cycle.address == 0x07FFEu) ++result->stack_7ffe_writes;
        } else if (cycle.type == V30_BUS_CYCLE_IO_WRITE) {
            if (!service_io_write(&pic, &pit, &cycle, result)) {
                remember_first_fault(result, result->cycles, &cycle);
                result->fail_reason = REV1_FAIL_IO;
                break;
            }
        } else {
            remember_first_fault(result, result->cycles, &cycle);
            result->fail_reason = REV1_FAIL_BUS_CYCLE;
            break;
        }

        if (!advance_pit(&pit, &pic, result)) {
            remember_first_fault(result, result->cycles, &cycle);
            result->fail_reason = REV1_FAIL_TERMINAL_COUNT;
            break;
        }
        ++result->cycles;
    }

    if (result->fail_reason == REV1_FAIL_NONE &&
        result->success_hits < SUCCESS_HITS_REQUIRED)
        result->fail_reason = REV1_FAIL_SUCCESS_TIMEOUT;

    if (result->fail_reason == REV1_FAIL_NONE &&
        result->min_ale_high_samples < MIN_ALE_HIGH_SAMPLES_REQ)
        result->fail_reason = REV1_FAIL_CAPTURE_MARGIN;

done:
    hold_reset(true);
    release_ad();
    set_intr(false);
    perf_clock_stop(clock);

    result->final_irr = pi86_pic_irr(&pic);
    result->final_isr = pi86_pic_isr(&pic);
    result->final_intr = pi86_pic_intr_asserted(&pic);

    result->pass = result->fail_reason == REV1_FAIL_NONE &&
                   result->cycle0_ok &&
                   result->first_read_ok &&
                   result->pit_programmed &&
                   result->no_irq_before_terminal &&
                   result->terminal_count &&
                   result->irq0_routed &&
                   result->inta_total == 2u &&
                   result->vector20 &&
                   result->ivt_offset &&
                   result->ivt_segment &&
                   result->isr_fetch &&
                   result->marker &&
                   result->eoi &&
                   result->stack_7ffa_writes >= 1u &&
                   result->stack_7ffc_writes >= 1u &&
                   result->stack_7ffe_writes >= 1u &&
                   result->stack_7ffa_reads >= 1u &&
                   result->stack_7ffc_reads >= 1u &&
                   result->stack_7ffe_reads >= 1u &&
                   result->min_ale_high_samples >= MIN_ALE_HIGH_SAMPLES_REQ &&
                   result->final_irr == 0u &&
                   result->final_isr == 0u &&
                   !result->final_intr &&
                   result->success_hits >= SUCCESS_HITS_REQUIRED;
    return result->pass;
}

static void dump_trace(const trace_ring_t *trace) {
    const uint32_t count = trace->write_count < TRACE_DEPTH ? trace->write_count : TRACE_DEPTH;
    const uint32_t first = trace->write_count > TRACE_DEPTH ? trace->write_count - TRACE_DEPTH : 0u;
    printf("\nLast %lu captured cycles (post-run):\n", (unsigned long)count);
    printf(" seq  address type lanes n  A19:16  T1_raw    CTRL_raw  CTRL_AD\n");
    for (uint32_t i = 0u; i < count; ++i) {
        const uint32_t seq = first + i;
        const trace_entry_t *e = &trace->entries[seq % TRACE_DEPTH];
        printf("%4lu  %05lX   %u    %u   %u    %X    %08lX  %08lX  %04X\n",
               (unsigned long)e->sequence,
               (unsigned long)e->address,
               (unsigned)e->cycle_type,
               (unsigned)e->lanes,
               (unsigned)e->ale_high_samples,
               (unsigned)e->high_nibble,
               (unsigned long)e->t1_raw,
               (unsigned long)e->control_raw,
               (unsigned)e->control_ad);
    }
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    init_control_outputs();
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPC1-A Rev1 - Controlled software-polling 0.300 MHz entry gate\n");
    printf("Configured V30 clock      : 0.300 MHz\n");
    printf("RESET hold                : %u complete V30 clocks\n", RESET_CLOCKS);
    printf("T1 capture                : last coherent ALE-high SIO snapshot\n");
    printf("Cycle-zero hard assertion : FFFF0 / MEM_READ / WORD\n");
    printf("Minimum capture margin    : min(n) >= %u ALE-high samples\n", MIN_ALE_HIGH_SAMPLES_REQ);
    printf("Timing-critical execution : interrupts masked; no USB printing in run\n");
    printf("Rev0 remains invalid and is not used as a performance baseline.\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);
    rev1_result_t result;
    trace_ring_t trace;

    const uint32_t irq_state = save_and_disable_interrupts();
    (void)run_point(&clock, &result, &trace);
    restore_interrupts(irq_state);

    printf("============================================================\n");
    printf("PC1-A REV1 0.300 MHz RESULT\n");
    printf("============================================================\n");
    printf("Cycle 0 FFFF0/read/word            = %s\n", result.cycle0_ok ? "PASS" : "FAIL");
    printf("Reset-vector read                   = %s\n", result.first_read_ok ? "PASS" : "FAIL");
    printf("PIT programmed                      = %s\n", result.pit_programmed ? "PASS" : "FAIL");
    printf("No IRQ0 before terminal count       = %s\n", result.no_irq_before_terminal ? "PASS" : "FAIL");
    printf("PIT terminal count / IRQ0 routed    = %s / %s\n",
           result.terminal_count ? "YES" : "NO",
           result.irq0_routed ? "YES" : "NO");
    printf("INTA total / vector 20h             = %u / %s\n",
           result.inta_total, result.vector20 ? "YES" : "NO");
    printf("IVT offset / segment                = %s / %s\n",
           result.ivt_offset ? "YES" : "NO",
           result.ivt_segment ? "YES" : "NO");
    printf("ISR fetch / marker / EOI            = %s / %s / %s\n",
           result.isr_fetch ? "YES" : "NO",
           result.marker ? "YES" : "NO",
           result.eoi ? "YES" : "NO");
    printf("Stack writes 7FFA/7FFC/7FFE         = %u / %u / %u\n",
           result.stack_7ffa_writes, result.stack_7ffc_writes, result.stack_7ffe_writes);
    printf("IRET reads 7FFA/7FFC/7FFE           = %u / %u / %u\n",
           result.stack_7ffa_reads, result.stack_7ffc_reads, result.stack_7ffe_reads);
    printf("Minimum ALE-high sample count n     = %u (required >= %u)\n",
           result.min_ale_high_samples, MIN_ALE_HIGH_SAMPLES_REQ);
    printf("Serviced bus cycles                 = %u/%u\n", result.cycles, MAX_BUS_CYCLES);
    printf("Success hits                        = %u/%u\n", result.success_hits, SUCCESS_HITS_REQUIRED);
    printf("Final IRR / ISR / INTR              = %02Xh / %02Xh / %u\n",
           result.final_irr, result.final_isr, result.final_intr ? 1u : 0u);
    printf("Failure reason                      = %s\n", fail_reason_name(result.fail_reason));
    if (result.first_fault_valid) {
        printf("First recorded fault                = seq=%lu address=%05lX type=%u lanes=%u\n",
               (unsigned long)result.first_fault_sequence,
               (unsigned long)result.first_fault_address,
               (unsigned)result.first_fault_type,
               (unsigned)result.first_fault_lanes);
    }
    printf("PC1-A REV1 0.300 MHz RESULT         = %s\n", result.pass ? "PASS" : "FAIL");

    dump_trace(&trace);

    printf("\nNOTE: independent ALE pulse counting, internal CLK self-measurement, and\n");
    printf("response/release latency instrumentation remain P2 follow-up items.\n");
    printf("Do not enable the 13-point Rev1 sweep until this single-point gate passes.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
