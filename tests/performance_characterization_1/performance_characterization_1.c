#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
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
#define PIT_TEST_COUNT             0x0008u
#define SUCCESS_LOOP_ADDR          0xF0033u

#define MAX_BUS_CYCLES             360u
#define SUCCESS_HITS_REQUIRED      3u
#define MAX_SIGNAL_SPINS           200000u
#define RESET_SETTLE_US            50u

static const uint32_t frequency_points_hz[] = {
    1000000u,
    2000000u,
    2500000u,
    3000000u,
    4000000u,
    4770000u,
    6000000u,
    8000000u,
};

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
    PERF_FAIL_NONE = 0,
    PERF_FAIL_ALE_TIMEOUT,
    PERF_FAIL_CONTROL_TIMEOUT,
    PERF_FAIL_BUS_CYCLE,
    PERF_FAIL_MEMORY,
    PERF_FAIL_IO,
    PERF_FAIL_PIC,
    PERF_FAIL_INTA,
    PERF_FAIL_TERMINAL_COUNT,
    PERF_FAIL_SUCCESS_TIMEOUT,
} perf_fail_reason_t;

typedef struct {
    uint32_t configured_hz;
    bool pass;
    perf_fail_reason_t fail_reason;
    uint cycles;
    uint success_hits;
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
    uint8_t final_irr;
    uint8_t final_isr;
    bool final_intr;
} perf_result_t;

static inline uint32_t sample_bit(uint32_t sample, uint gpio) {
    return (sample >> gpio) & 1u;
}

static const char *fail_reason_name(perf_fail_reason_t reason) {
    switch (reason) {
        case PERF_FAIL_NONE: return "none";
        case PERF_FAIL_ALE_TIMEOUT: return "ALE timeout / host missed bus timing";
        case PERF_FAIL_CONTROL_TIMEOUT: return "control phase timeout";
        case PERF_FAIL_BUS_CYCLE: return "unsupported/corrupted bus cycle";
        case PERF_FAIL_MEMORY: return "memory transaction failure";
        case PERF_FAIL_IO: return "I/O transaction failure";
        case PERF_FAIL_PIC: return "PIC state/sequencing failure";
        case PERF_FAIL_INTA: return "interrupt acknowledge failure";
        case PERF_FAIL_TERMINAL_COUNT: return "PIT terminal-count routing failure";
        case PERF_FAIL_SUCCESS_TIMEOUT: return "SUCCESS not reached before cycle limit";
        default: return "unknown";
    }
}

static void init_data_path(void) {
    low_lane_mask_value = 0u;
    high_lane_mask_value = 0u;

    for (uint bit = 0; bit < 8u; ++bit) {
        low_lane_mask_value |= 1u << ad_gpio[bit];
        high_lane_mask_value |= 1u << ad_gpio[bit + 8u];
    }

    for (uint32_t value = 0; value < 256u; ++value) {
        uint32_t lo = 0u;
        uint32_t hi = 0u;
        for (uint bit = 0; bit < 8u; ++bit) {
            if (((value >> bit) & 1u) != 0u) {
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

static void hold_reset(bool asserted) {
    gpio_init(V30_PIN_RESET);
    gpio_disable_pulls(V30_PIN_RESET);
    gpio_put(V30_PIN_RESET, asserted);
    gpio_set_dir(V30_PIN_RESET, GPIO_OUT);
}

static void set_intr(bool asserted) {
    gpio_init(V30_PIN_INTR);
    gpio_disable_pulls(V30_PIN_INTR);
    gpio_put(V30_PIN_INTR, asserted);
    gpio_set_dir(V30_PIN_INTR, GPIO_OUT);
}

static void release_ad(void) {
    sio_hw->gpio_oe_clr = V30_AD_BUS_MASK;
}

static void drive_data(uint16_t value, v30_bus_lanes_t lanes) {
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

static uint16_t decode_ad(uint32_t sample) {
    uint16_t value = 0u;
    for (uint bit = 0; bit < 16u; ++bit)
        value |= (uint16_t)(sample_bit(sample, ad_gpio[bit]) << bit);
    return value;
}

static uint32_t decode_address(uint32_t sample) {
    uint32_t address = decode_ad(sample);
    address |= sample_bit(sample, V30_PIN_A16) << 16;
    address |= sample_bit(sample, V30_PIN_A17) << 17;
    address |= sample_bit(sample, V30_PIN_A18) << 18;
    address |= sample_bit(sample, V30_PIN_A19) << 19;
    return address & 0xFFFFFu;
}

static v30_bus_lanes_t decode_lanes(uint8_t a0, uint8_t bhe_n) {
    if (a0 == 0u && bhe_n == 0u) return V30_BUS_LANES_WORD;
    if (a0 == 0u && bhe_n == 1u) return V30_BUS_LANE_LOW;
    if (a0 == 1u && bhe_n == 0u) return V30_BUS_LANE_HIGH;
    return V30_BUS_LANES_NONE;
}

static v30_bus_cycle_type_t decode_cycle_type(uint8_t iom,
                                               uint8_t dtr,
                                               uint8_t inta_n) {
    if (inta_n == 0u) return V30_BUS_CYCLE_INTERRUPT_ACK;
    if (iom != 0u)
        return dtr == 0u ? V30_BUS_CYCLE_MEM_READ : V30_BUS_CYCLE_MEM_WRITE;
    return dtr == 0u ? V30_BUS_CYCLE_IO_READ : V30_BUS_CYCLE_IO_WRITE;
}

static bool wait_signal_level(uint gpio, bool level, uint32_t *sample_out) {
    for (uint32_t spins = 0u; spins < MAX_SIGNAL_SPINS; ++spins) {
        const uint32_t sample = sio_hw->gpio_in;
        if ((sample_bit(sample, gpio) != 0u) == level) {
            if (sample_out != NULL) *sample_out = sample;
            return true;
        }
    }
    return false;
}

static bool wait_falling_edges(uint count) {
    for (uint edge = 0u; edge < count; ++edge) {
        if (!wait_signal_level(V30_PIN_CLK, true, NULL)) return false;
        if (!wait_signal_level(V30_PIN_CLK, false, NULL)) return false;
    }
    return true;
}

static bool capture_cycle(bool first_cycle,
                          v30_bus_cycle_t *cycle,
                          perf_fail_reason_t *reason) {
    uint32_t t1 = 0u;
    uint32_t control = 0u;

    if (!first_cycle) {
        if (!wait_signal_level(V30_PIN_ALE, false, NULL)) {
            *reason = PERF_FAIL_ALE_TIMEOUT;
            return false;
        }
    }

    if (!wait_signal_level(V30_PIN_ALE, true, &t1)) {
        *reason = PERF_FAIL_ALE_TIMEOUT;
        return false;
    }

    cycle->t1_sample = t1;
    cycle->address = decode_address(t1);
    cycle->a0 = (uint8_t)sample_bit(t1, V30_PIN_AD0);
    cycle->bhe_n = (uint8_t)sample_bit(t1, V30_PIN_BHE);
    cycle->lanes = decode_lanes(cycle->a0, cycle->bhe_n);

    if (!wait_signal_level(V30_PIN_ALE, false, &control)) {
        *reason = PERF_FAIL_CONTROL_TIMEOUT;
        return false;
    }

    cycle->control_sample = control;
    cycle->iom = (uint8_t)sample_bit(control, V30_PIN_IOM);
    cycle->dtr = (uint8_t)sample_bit(control, V30_PIN_DTR);
    cycle->inta_n = (uint8_t)sample_bit(control, V30_PIN_INTA);
    cycle->type = decode_cycle_type(cycle->iom, cycle->dtr, cycle->inta_n);
    cycle->idle_steps = 0u;

    if (cycle->lanes == V30_BUS_LANES_NONE && cycle->type != V30_BUS_CYCLE_INTERRUPT_ACK) {
        *reason = PERF_FAIL_BUS_CYCLE;
        return false;
    }
    return true;
}

static void perf_clock_init(perf_clock_t *clock, PIO pio) {
    clock->pio = pio;
    clock->sm = pio_claim_unused_sm(pio, true);
    clock->offset = pio_add_program(pio, &perf_continuous_clk_program);
}

static void perf_clock_start(perf_clock_t *clock, uint32_t v30_hz) {
    pio_sm_set_enabled(clock->pio, clock->sm, false);

    gpio_init(V30_PIN_CLK);
    gpio_disable_pulls(V30_PIN_CLK);
    gpio_put(V30_PIN_CLK, false);
    gpio_set_dir(V30_PIN_CLK, GPIO_OUT);

    pio_sm_config c = perf_continuous_clk_program_get_default_config(clock->offset);
    sm_config_set_set_pins(&c, V30_PIN_CLK, 1u);
    sm_config_set_clkdiv(&c,
        (float)clock_get_hz(clk_sys) / (2.0f * (float)v30_hz));

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

    static const uint8_t irq0_isr[] = {
        0xC6, 0x06, 0x00, 0x03, 0xA0,
        0xB0, 0x20,
        0xE6, 0x20,
        0xCF,
    };
    for (uint32_t i = 0u; i < sizeof(irq0_isr); ++i)
        rom[IRQ0_ISR_OFFSET + i] = irq0_isr[i];

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
    if (cycle->lanes == V30_BUS_LANE_LOW) {
        uint8_t v = 0u;
        if (!pi86_memory_read8(memory, cycle->address, &v)) return false;
        *value = v;
        return true;
    }
    if (cycle->lanes == V30_BUS_LANE_HIGH) {
        uint8_t v = 0u;
        if (!pi86_memory_read8(memory, cycle->address, &v)) return false;
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

static bool extract_byte(const v30_bus_cycle_t *cycle, uint16_t data, uint8_t *value) {
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

static bool service_inta(pi86_pic_t *pic, perf_result_t *result) {
    bool drive_vector = false;
    uint8_t vector = 0u;
    const uint8_t phase_before = pi86_pic_acknowledge_phase(pic);

    if (!pi86_pic_begin_inta(pic, &drive_vector, &vector))
        return false;

    ++result->inta_total;

    if (drive_vector)
        drive_data(vector, V30_BUS_LANE_LOW);
    else
        release_ad();

    if (!wait_signal_level(V30_PIN_INTA, true, NULL)) {
        release_ad();
        return false;
    }

    release_ad();

    if (!pi86_pic_end_inta(pic))
        return false;

    if (phase_before == 1u && drive_vector && vector == IRQ0_VECTOR)
        result->vector20 = true;

    set_intr(pi86_pic_intr_asserted(pic));
    return true;
}

static bool service_io_write(pi86_pic_t *pic,
                             pi86_pit_t *pit,
                             const v30_bus_cycle_t *cycle,
                             perf_result_t *result) {
    const uint16_t port = (uint16_t)(cycle->address & 0xFFFFu);
    const uint16_t raw = decode_ad(cycle->control_sample);
    uint8_t value = 0u;
    if (!extract_byte(cycle, raw, &value)) return false;

    if (port == PI86_PIC_COMMAND_PORT || port == PI86_PIC_DATA_PORT) {
        const uint8_t isr_before = pi86_pic_isr(pic);
        if (!pi86_pic_io_write8(pic, port, value)) return false;
        if (port == PI86_PIC_COMMAND_PORT && value == 0x20u &&
            (isr_before & 0x01u) != 0u && pi86_pic_isr(pic) == 0u) {
            result->eoi = true;
        }
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
                        perf_result_t *result) {
    if (!pi86_pit_programmed(pit) || !pi86_pit_counting(pit))
        return true;

    if (pi86_pic_intr_asserted(pic))
        result->no_irq_before_terminal = false;

    pi86_pit_tick(pit);
    if (!pi86_pit_take_terminal_count(pit))
        return true;

    result->terminal_count = true;
    if (!pi86_pic_raise_irq(pic, IRQ0_LINE))
        return false;

    result->irq0_routed = true;
    set_intr(pi86_pic_intr_asserted(pic));
    return true;
}

static bool run_frequency_point(perf_clock_t *clock,
                                uint32_t frequency_hz,
                                perf_result_t *result) {
    *result = (perf_result_t){0};
    result->configured_hz = frequency_hz;
    result->no_irq_before_terminal = true;

    init_test_image();

    pi86_memory_t memory;
    pi86_memory_init(&memory, ram, RAM_BASE, RAM_SIZE,
                     rom, PI86_ROM_BASE, ROM_SIZE);

    pi86_pic_t pic;
    pi86_pic_init(&pic, VECTOR_BASE);

    pi86_pit_t pit;
    pi86_pit_init(&pit);

    release_ad();
    set_intr(false);
    hold_reset(true);
    perf_clock_start(clock, frequency_hz);
    busy_wait_us_32(RESET_SETTLE_US);
    hold_reset(false);

    bool first_cycle = true;

    while (result->cycles < MAX_BUS_CYCLES &&
           result->success_hits < SUCCESS_HITS_REQUIRED) {
        v30_bus_cycle_t cycle;
        perf_fail_reason_t capture_reason = PERF_FAIL_NONE;
        if (!capture_cycle(first_cycle, &cycle, &capture_reason)) {
            result->fail_reason = capture_reason;
            break;
        }
        first_cycle = false;

        if (cycle.type == V30_BUS_CYCLE_INTERRUPT_ACK) {
            if (!service_inta(&pic, result)) {
                result->fail_reason = PERF_FAIL_INTA;
                break;
            }
            ++result->cycles;
            continue;
        }

        if (cycle.type == V30_BUS_CYCLE_MEM_READ) {
            uint16_t driven = 0u;
            if (!memory_read(&memory, &cycle, &driven)) {
                result->fail_reason = PERF_FAIL_MEMORY;
                break;
            }

            if (result->cycles == 0u)
                result->first_read_ok = cycle.address == RESET_VECTOR_ADDR &&
                                        cycle.lanes == V30_BUS_LANES_WORD;

            drive_data(driven, cycle.lanes);
            if (!wait_falling_edges(2u)) {
                release_ad();
                result->fail_reason = PERF_FAIL_CONTROL_TIMEOUT;
                break;
            }
            release_ad();

            if (cycle.address == IRQ0_IVT_ENTRY_ADDR && driven == IRQ0_ISR_OFFSET)
                result->ivt_offset = true;
            if (cycle.address == IRQ0_IVT_ENTRY_ADDR + 2u && driven == ISR_SEGMENT)
                result->ivt_segment = true;
            if (cycle.address == IRQ0_ISR_ADDR)
                result->isr_fetch = true;
            if (cycle.address == IRQ0_MARKER_ADDR &&
                (uint8_t)driven == IRQ0_MARKER_VALUE)
                result->marker = true;
            if (cycle.address == SUCCESS_LOOP_ADDR)
                ++result->success_hits;

            if (cycle.address == 0x07FFAu) ++result->stack_7ffa_reads;
            if (cycle.address == 0x07FFCu) ++result->stack_7ffc_reads;
            if (cycle.address == 0x07FFEu) ++result->stack_7ffe_reads;
        } else if (cycle.type == V30_BUS_CYCLE_MEM_WRITE) {
            const uint16_t data = decode_ad(cycle.control_sample);
            if (!memory_write(&memory, &cycle, data)) {
                result->fail_reason = PERF_FAIL_MEMORY;
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
                result->fail_reason = PERF_FAIL_IO;
                break;
            }
        } else {
            result->fail_reason = PERF_FAIL_BUS_CYCLE;
            break;
        }

        if (!advance_pit(&pit, &pic, result)) {
            result->fail_reason = PERF_FAIL_TERMINAL_COUNT;
            break;
        }

        ++result->cycles;
    }

    hold_reset(true);
    release_ad();
    set_intr(false);
    busy_wait_us_32(10u);
    perf_clock_stop(clock);

    result->final_irr = pi86_pic_irr(&pic);
    result->final_isr = pi86_pic_isr(&pic);
    result->final_intr = pi86_pic_intr_asserted(&pic);

    if (result->fail_reason == PERF_FAIL_NONE &&
        result->success_hits < SUCCESS_HITS_REQUIRED)
        result->fail_reason = PERF_FAIL_SUCCESS_TIMEOUT;

    result->pass = result->fail_reason == PERF_FAIL_NONE &&
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
                   result->final_irr == 0u &&
                   result->final_isr == 0u &&
                   !result->final_intr &&
                   result->success_hits >= SUCCESS_HITS_REQUIRED;

    return result->pass;
}

static void print_mhz(uint32_t hz) {
    printf("%lu.%03lu MHz",
           (unsigned long)(hz / 1000000u),
           (unsigned long)((hz % 1000000u) / 1000u));
}

static void print_point_result(const perf_result_t *r, uint index, uint total) {
    printf("\n============================================================\n");
    printf("Performance Characterization 1 - point %u/%u\n", index + 1u, total);
    printf("Configured V30 clock : ");
    print_mhz(r->configured_hz);
    printf("\n");
    printf("Clock mode           : continuous PIO free-running\n");
    printf("============================================================\n");

    printf("Reset-vector read                 = %s\n", r->first_read_ok ? "PASS" : "FAIL");
    printf("PIT programmed                    = %s\n", r->pit_programmed ? "PASS" : "FAIL");
    printf("No IRQ0 before terminal count     = %s\n", r->no_irq_before_terminal ? "PASS" : "FAIL");
    printf("PIT terminal count / IRQ0 routed  = %s / %s\n",
           r->terminal_count ? "YES" : "NO",
           r->irq0_routed ? "YES" : "NO");
    printf("INTA total / vector 20h           = %u / %s\n",
           r->inta_total, r->vector20 ? "YES" : "NO");
    printf("IVT offset / segment              = %s / %s\n",
           r->ivt_offset ? "YES" : "NO",
           r->ivt_segment ? "YES" : "NO");
    printf("ISR fetch / marker / EOI          = %s / %s / %s\n",
           r->isr_fetch ? "YES" : "NO",
           r->marker ? "YES" : "NO",
           r->eoi ? "YES" : "NO");
    printf("Stack writes 7FFA/7FFC/7FFE       = %u / %u / %u\n",
           r->stack_7ffa_writes, r->stack_7ffc_writes, r->stack_7ffe_writes);
    printf("IRET reads 7FFA/7FFC/7FFE         = %u / %u / %u\n",
           r->stack_7ffa_reads, r->stack_7ffc_reads, r->stack_7ffe_reads);
    printf("Final IRR / ISR / INTR            = %02Xh / %02Xh / %u\n",
           r->final_irr, r->final_isr, r->final_intr ? 1u : 0u);
    printf("Serviced bus cycles               = %u/%u\n", r->cycles, MAX_BUS_CYCLES);
    printf("Success hits                      = %u/%u\n",
           r->success_hits, SUCCESS_HITS_REQUIRED);
    printf("Failure reason                    = %s\n", fail_reason_name(r->fail_reason));
    printf("RESULT @ ");
    print_mhz(r->configured_hz);
    printf(" = %s\n", r->pass ? "PASS" : "FAIL");
    fflush(stdout);
}

int main(void) {
    prepare_header_high_z();
    init_data_path();
    hold_reset(true);
    set_intr(false);
    release_ad();

    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    printf("\nPerformance Characterization 1 - Maximum Sustainable V30 Clock\n");
    printf("One firmware run sweeps all configured clock points.\n");
    printf("No per-bus-cycle USB logging occurs while a point is running.\n");
    printf("Configured clock is reported explicitly; it is not claimed as an independently measured frequency.\n");
    printf("Gate 12 stepped-clock firmware remains the last-known-good functional regression baseline.\n\n");
    fflush(stdout);

    perf_clock_t clock;
    perf_clock_init(&clock, pio0);

    const uint point_count = (uint)(sizeof(frequency_points_hz) /
                                    sizeof(frequency_points_hz[0]));
    perf_result_t results[sizeof(frequency_points_hz) /
                          sizeof(frequency_points_hz[0])];

    int last_good = -1;
    int first_fail = -1;

    for (uint i = 0u; i < point_count; ++i) {
        printf("\n>>> Starting point %u/%u: ", i + 1u, point_count);
        print_mhz(frequency_points_hz[i]);
        printf(" configured V30 clock <<<\n");
        fflush(stdout);

        (void)run_frequency_point(&clock, frequency_points_hz[i], &results[i]);
        print_point_result(&results[i], i, point_count);

        if (results[i].pass)
            last_good = (int)i;
        else if (first_fail < 0)
            first_fail = (int)i;

        sleep_ms(100);
    }

    hold_reset(true);
    set_intr(false);
    release_ad();
    perf_clock_stop(&clock);

    printf("\n============================================================\n");
    printf("PERFORMANCE CHARACTERIZATION 1 SUMMARY\n");
    printf("============================================================\n");
    for (uint i = 0u; i < point_count; ++i) {
        print_mhz(results[i].configured_hz);
        printf("   %s", results[i].pass ? "PASS" : "FAIL");
        if (!results[i].pass)
            printf("   (%s)", fail_reason_name(results[i].fail_reason));
        printf("\n");
    }

    printf("\nLast known-good configured clock : ");
    if (last_good >= 0) print_mhz(results[last_good].configured_hz);
    else printf("none");
    printf("\nFirst failing configured clock    : ");
    if (first_fail >= 0) print_mhz(results[first_fail].configured_hz);
    else printf("none in sweep");
    printf("\n");
    printf("Physical CLK should be scope/counter verified before any configured value is reported as measured frequency.\n");
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
    fflush(stdout);

    while (true) tight_loop_contents();
}
