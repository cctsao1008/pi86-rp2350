#include "io/io.h"

static bool port_in_range(const pi86_io_t *io, uint16_t port) {
    const uint32_t p = port;
    return io->size != 0u && p >= io->base && (p - io->base) < io->size;
}

void pi86_io_init(pi86_io_t *io,
                  uint8_t *storage,
                  uint32_t base,
                  uint32_t size) {
    io->storage = storage;
    io->base = base;
    io->size = size;
}

bool pi86_io_read8(const pi86_io_t *io,
                   uint16_t port,
                   uint8_t *value) {
    if (!port_in_range(io, port)) return false;
    *value = io->storage[(uint32_t)port - io->base];
    return true;
}

bool pi86_io_write8(pi86_io_t *io,
                    uint16_t port,
                    uint8_t value) {
    if (!port_in_range(io, port)) return false;
    io->storage[(uint32_t)port - io->base] = value;
    return true;
}
