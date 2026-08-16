#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Byte-addressed x86 I/O-port backend.
 *
 * The V30 bus layer owns electrical/timing/lane semantics. This backend owns
 * only the logical 16-bit I/O-port address space used by synthetic bring-up
 * devices and later PC-compatible peripheral models.
 */

typedef struct {
    uint8_t *storage;
    uint32_t base;
    uint32_t size;
} pi86_io_t;

void pi86_io_init(pi86_io_t *io,
                  uint8_t *storage,
                  uint32_t base,
                  uint32_t size);

bool pi86_io_read8(const pi86_io_t *io,
                   uint16_t port,
                   uint8_t *value);

bool pi86_io_write8(pi86_io_t *io,
                    uint16_t port,
                    uint8_t value);
