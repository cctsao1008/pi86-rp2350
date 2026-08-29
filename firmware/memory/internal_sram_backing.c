#include "memory/internal_sram_backing.h"

#include <stdalign.h>

/*
 * This pool is intentionally separate from firmware-private allocations.  A
 * fixed upper bound makes linker overflow visible at build time and leaves
 * roughly 224 KiB of main SRAM for firmware/realtime growth in the current
 * canonical image.
 */
/* Until Host upload is connected to canonical main.c, link-time optimization
 * can prove that no byte is read and discard the whole pool. The reservation
 * is part of the runtime memory contract, so the init-time volatile access
 * below keeps it visible in the linker map before that integration. */
static alignas(32) uint8_t processor_memory[RP86_INTERNAL_SRAM_PROCESSOR_SIZE]
    __attribute__((used));

void rp86_internal_sram_backing_init(rp86_memory_backing_t *backing) {
    *(volatile uint8_t *)processor_memory = 0u;
    rp86_memory_backing_init_direct(backing, "INTERNAL SRAM",
                                    RP86_INTERNAL_SRAM_PROCESSOR_BASE,
                                    processor_memory,
                                    sizeof processor_memory);
}
