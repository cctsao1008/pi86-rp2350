#include "memory/memory.h"

static bool range_contains(uint32_t base, uint32_t size, uint32_t address) {
    return size != 0u && address >= base && (address - base) < size;
}

void pi86_memory_init(pi86_memory_t *memory,
                      uint8_t *ram,
                      uint32_t ram_base,
                      uint32_t ram_size,
                      const uint8_t *rom,
                      uint32_t rom_base,
                      uint32_t rom_size) {
    memory->ram = ram;
    memory->ram_base = ram_base;
    memory->ram_size = ram_size;
    memory->rom = rom;
    memory->rom_base = rom_base;
    memory->rom_size = rom_size;
}

bool pi86_memory_read8(const pi86_memory_t *memory,
                       uint32_t address,
                       uint8_t *value) {
    if (range_contains(memory->ram_base, memory->ram_size, address)) {
        *value = memory->ram[address - memory->ram_base];
        return true;
    }

    if (range_contains(memory->rom_base, memory->rom_size, address)) {
        *value = memory->rom[address - memory->rom_base];
        return true;
    }

    return false;
}

bool pi86_memory_write8(pi86_memory_t *memory,
                        uint32_t address,
                        uint8_t value) {
    if (!range_contains(memory->ram_base, memory->ram_size, address)) return false;
    memory->ram[address - memory->ram_base] = value;
    return true;
}

bool pi86_memory_read16(const pi86_memory_t *memory,
                        uint32_t address,
                        uint16_t *value) {
    uint8_t lo = 0u;
    uint8_t hi = 0u;
    if (!pi86_memory_read8(memory, address, &lo)) return false;
    if (!pi86_memory_read8(memory, address + 1u, &hi)) return false;
    *value = (uint16_t)lo | ((uint16_t)hi << 8);
    return true;
}

bool pi86_memory_write16(pi86_memory_t *memory,
                         uint32_t address,
                         uint16_t value) {
    if (!range_contains(memory->ram_base, memory->ram_size, address)) return false;
    if (!range_contains(memory->ram_base, memory->ram_size, address + 1u)) return false;

    const uint32_t offset = address - memory->ram_base;
    memory->ram[offset] = (uint8_t)(value & 0xFFu);
    memory->ram[offset + 1u] = (uint8_t)(value >> 8);
    return true;
}
