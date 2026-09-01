#include "runtime/prepared_runtime.h"

#include <string.h>

#include "host_protocol/host_protocol.h"

void rp86_prepared_runtime_init(rp86_prepared_runtime_t *runtime) {
    memset(runtime, 0, sizeof *runtime);
    runtime->available = true;
}

void rp86_prepared_runtime_mark_initialized(
        rp86_prepared_runtime_t *runtime) {
    runtime->initialized = true;
}

void rp86_prepared_runtime_activate(rp86_prepared_runtime_t *runtime) {
    runtime->available = true;
}

void rp86_prepared_runtime_retire(rp86_prepared_runtime_t *runtime) {
    runtime->available = false;
}

bool rp86_prepared_runtime_initialized(
        const rp86_prepared_runtime_t *runtime) {
    return runtime->initialized;
}

bool rp86_prepared_runtime_available(
        const rp86_prepared_runtime_t *runtime) {
    return runtime->available;
}

bool rp86_prepared_runtime_physically_running(
        const rp86_prepared_runtime_t *runtime,
        bool bus_active,
        bool clock_free_running,
        bool reset_asserted) {
    return runtime->available && bus_active && clock_free_running &&
        !reset_asserted;
}

bool rp86_prepared_processor_signature_valid(uint16_t signature) {
    return signature == RP86_PROCESSOR_SIGNATURE_INTEL_8086 ||
        signature == RP86_PROCESSOR_SIGNATURE_NEC_V30;
}

const char *rp86_prepared_processor_identity_name(uint16_t signature) {
    if (signature == RP86_PROCESSOR_SIGNATURE_INTEL_8086)
        return "INTEL 8086";
    if (signature == RP86_PROCESSOR_SIGNATURE_NEC_V30)
        return "NEC V30";
    return "UNKNOWN";
}

uint8_t rp86_prepared_processor_witness_flags(uint16_t signature) {
    if (signature == RP86_PROCESSOR_SIGNATURE_INTEL_8086)
        return RP86_NATIVE_WITNESS_PROCESSOR_INTEL_8086;
    if (signature == RP86_PROCESSOR_SIGNATURE_NEC_V30)
        return RP86_NATIVE_WITNESS_PROCESSOR_NEC_V30;
    return 0u;
}

void rp86_prepared_runtime_observe_processor(
        rp86_prepared_runtime_t *runtime,
        uint16_t signature) {
    runtime->processor_signature = signature;
    runtime->processor_identity_valid =
        rp86_prepared_processor_signature_valid(signature);
}
