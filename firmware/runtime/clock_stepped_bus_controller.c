#include "runtime/clock_stepped_bus_controller.h"

#include <string.h>

static bool lane_read(const rp86_memory_t *memory,
                      const rp86_processor_bus_cycle_t *cycle,
                      uint16_t *driven) {
    if (cycle->lanes == RP86_PROCESSOR_BUS_LANES_WORD)
        return rp86_memory_read16(memory, cycle->address, driven);

    uint8_t value = 0u;
    if (cycle->lanes == RP86_PROCESSOR_BUS_LANE_LOW) {
        if (!rp86_memory_read8(memory, cycle->address, &value)) return false;
        *driven = value;
        return true;
    }
    if (cycle->lanes == RP86_PROCESSOR_BUS_LANE_HIGH) {
        if (!rp86_memory_read8(memory, cycle->address, &value)) return false;
        *driven = (uint16_t)value << 8u;
        return true;
    }
    return false;
}

static bool lane_write(rp86_memory_t *memory,
                       const rp86_processor_bus_cycle_t *cycle,
                       uint16_t value) {
    if (cycle->lanes == RP86_PROCESSOR_BUS_LANES_WORD)
        return rp86_memory_write16(memory, cycle->address, value);
    if (cycle->lanes == RP86_PROCESSOR_BUS_LANE_LOW)
        return rp86_memory_write8(memory, cycle->address, (uint8_t)value);
    if (cycle->lanes == RP86_PROCESSOR_BUS_LANE_HIGH)
        return rp86_memory_write8(memory, cycle->address,
                                  (uint8_t)(value >> 8u));
    return false;
}

static bool readback_matches(rp86_processor_bus_lanes_t lanes,
                             uint16_t driven, uint16_t sampled) {
    if (lanes == RP86_PROCESSOR_BUS_LANES_WORD) return driven == sampled;
    if (lanes == RP86_PROCESSOR_BUS_LANE_LOW)
        return (uint8_t)driven == (uint8_t)sampled;
    if (lanes == RP86_PROCESSOR_BUS_LANE_HIGH)
        return (uint8_t)(driven >> 8u) == (uint8_t)(sampled >> 8u);
    return false;
}

static bool complete_read(rp86_processor_bus_t *bus, const rp86_processor_bus_cycle_t *cycle,
                          uint16_t driven, rp86_clock_stepped_stats_t *stats) {
    rp86_processor_bus_drive_data(driven, cycle->lanes);
    uint16_t rb1 = 0u;
    uint16_t rb2 = 0u;
    rp86_processor_bus_complete_read(bus, &rb1, &rb2);
    if (!readback_matches(cycle->lanes, driven, rb1) ||
        !readback_matches(cycle->lanes, driven, rb2)) {
        stats->pad_mismatch = true;
        return false;
    }
    return true;
}

bool rp86_clock_stepped_service_cycle(rp86_processor_bus_t *bus,
                              rp86_memory_t *memory,
                              const rp86_clock_stepped_io_t *io,
                              uint max_idle_steps,
                              rp86_clock_stepped_stats_t *stats) {
    if (bus == NULL || memory == NULL || stats == NULL) return false;

    rp86_processor_bus_cycle_t cycle;
    memset(&cycle, 0, sizeof cycle);
    if (!rp86_processor_bus_wait_cycle(bus, max_idle_steps, &cycle)) return false;

    if (!stats->first_cycle_seen) {
        stats->first_cycle_seen = true;
        stats->first_address = cycle.address;
    }
    stats->last_address = cycle.address;
    stats->last_type = cycle.type;
    stats->last_lanes = cycle.lanes;
    stats->last_data_valid = false;

    if (cycle.lanes == RP86_PROCESSOR_BUS_LANES_NONE) {
        stats->invalid_lane = true;
        return false;
    }

    if (cycle.type == RP86_PROCESSOR_BUS_CYCLE_MEM_READ) {
        uint16_t driven = 0u;
        if (!lane_read(memory, &cycle, &driven)) {
            stats->unmapped = true;
            return false;
        }
        stats->last_data = driven;
        stats->last_data_valid = true;
        if (!complete_read(bus, &cycle, driven, stats)) return false;
        ++stats->memory_reads;
    } else if (cycle.type == RP86_PROCESSOR_BUS_CYCLE_MEM_WRITE) {
        uint16_t d0 = 0u;
        rp86_processor_bus_complete_write(bus, &cycle, &d0, NULL, NULL);
        stats->last_data = d0;
        stats->last_data_valid = true;
        if (!lane_write(memory, &cycle, d0)) {
            stats->unmapped = true;
            return false;
        }
        ++stats->memory_writes;
    } else if (cycle.type == RP86_PROCESSOR_BUS_CYCLE_IO_READ) {
        uint16_t driven = 0u;
        if (io == NULL || io->read == NULL ||
            !io->read(io->context, (uint16_t)cycle.address,
                      cycle.lanes, &driven)) {
            stats->unmapped = true;
            return false;
        }
        stats->last_data = driven;
        stats->last_data_valid = true;
        if (!complete_read(bus, &cycle, driven, stats)) return false;
        ++stats->io_reads;
    } else if (cycle.type == RP86_PROCESSOR_BUS_CYCLE_IO_WRITE) {
        uint16_t d0 = 0u;
        rp86_processor_bus_complete_write(bus, &cycle, &d0, NULL, NULL);
        stats->last_data = d0;
        stats->last_data_valid = true;
        if (io == NULL || io->write == NULL ||
            !io->write(io->context, (uint16_t)cycle.address,
                       cycle.lanes, d0)) {
            stats->unmapped = true;
            return false;
        }
        ++stats->io_writes;
    } else {
        stats->unmapped = true;
        return false;
    }

    ++stats->cycles;
    return true;
}

const char *rp86_clock_stepped_cycle_name(rp86_processor_bus_cycle_type_t type) {
    switch (type) {
        case RP86_PROCESSOR_BUS_CYCLE_MEM_READ: return "MEMR";
        case RP86_PROCESSOR_BUS_CYCLE_MEM_WRITE: return "MEMW";
        case RP86_PROCESSOR_BUS_CYCLE_IO_READ: return "IOR";
        case RP86_PROCESSOR_BUS_CYCLE_IO_WRITE: return "IOW";
        case RP86_PROCESSOR_BUS_CYCLE_INTERRUPT_ACK: return "INTA";
        default: return "UNSUPPORTED";
    }
}
