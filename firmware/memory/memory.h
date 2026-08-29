#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Simple byte-addressed backend used by bring-up tests and future system code.
 * The bus layer owns lane/timing semantics; this layer owns addressable storage.
 */

typedef struct {
    uint8_t *ram;
    uint32_t ram_base;
    uint32_t ram_size;

    const uint8_t *rom;
    uint32_t rom_base;
    uint32_t rom_size;
} rp86_memory_t;

void rp86_memory_init(rp86_memory_t *memory,
                      uint8_t *ram,
                      uint32_t ram_base,
                      uint32_t ram_size,
                      const uint8_t *rom,
                      uint32_t rom_base,
                      uint32_t rom_size);

bool rp86_memory_read8(const rp86_memory_t *memory,
                       uint32_t address,
                       uint8_t *value);

bool rp86_memory_write8(rp86_memory_t *memory,
                        uint32_t address,
                        uint8_t value);

bool rp86_memory_read16(const rp86_memory_t *memory,
                        uint32_t address,
                        uint16_t *value);

bool rp86_memory_write16(rp86_memory_t *memory,
                         uint32_t address,
                         uint16_t value);
