#ifndef RP86_PREPARED_RUNTIME_H
#define RP86_PREPARED_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#define RP86_PROCESSOR_SIGNATURE_INTEL_8086 0x0012u
#define RP86_PROCESSOR_SIGNATURE_NEC_V30    0x000Cu

typedef struct {
    bool initialized;
    bool available;
    uint16_t processor_signature;
    bool processor_identity_valid;
} rp86_prepared_runtime_t;

void rp86_prepared_runtime_init(rp86_prepared_runtime_t *runtime);
void rp86_prepared_runtime_mark_initialized(
    rp86_prepared_runtime_t *runtime);
void rp86_prepared_runtime_activate(rp86_prepared_runtime_t *runtime);
void rp86_prepared_runtime_retire(rp86_prepared_runtime_t *runtime);

bool rp86_prepared_runtime_initialized(
    const rp86_prepared_runtime_t *runtime);
bool rp86_prepared_runtime_available(
    const rp86_prepared_runtime_t *runtime);
bool rp86_prepared_runtime_physically_running(
    const rp86_prepared_runtime_t *runtime,
    bool bus_active,
    bool clock_free_running,
    bool reset_asserted);

bool rp86_prepared_processor_signature_valid(uint16_t signature);
const char *rp86_prepared_processor_identity_name(uint16_t signature);
uint8_t rp86_prepared_processor_witness_flags(uint16_t signature);
void rp86_prepared_runtime_observe_processor(
    rp86_prepared_runtime_t *runtime,
    uint16_t signature);

#endif
