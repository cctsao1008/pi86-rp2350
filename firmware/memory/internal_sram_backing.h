#ifndef RP86_INTERNAL_SRAM_BACKING_H
#define RP86_INTERNAL_SRAM_BACKING_H

#include <stdint.h>

#include "memory/backing.h"

enum {
    /* First canonical processor-memory tier. It covers the IVT and low-memory
     * workload space without consuming the complete RP2350 SRAM budget. */
    RP86_INTERNAL_SRAM_PROCESSOR_BASE = 0x00000u,
    RP86_INTERNAL_SRAM_PROCESSOR_SIZE = 256u * 1024u,
};

void rp86_internal_sram_backing_init(rp86_memory_backing_t *backing);

#endif
