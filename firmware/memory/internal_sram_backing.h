#ifndef PI86_INTERNAL_SRAM_BACKING_H
#define PI86_INTERNAL_SRAM_BACKING_H

#include <stdint.h>

#include "memory/backing.h"

enum {
    /* First canonical processor-memory tier. It covers the IVT and low-memory
     * workload space without consuming the complete RP2350 SRAM budget. */
    PI86_INTERNAL_SRAM_PROCESSOR_BASE = 0x00000u,
    PI86_INTERNAL_SRAM_PROCESSOR_SIZE = 256u * 1024u,
};

void pi86_internal_sram_backing_init(pi86_memory_backing_t *backing);

#endif
