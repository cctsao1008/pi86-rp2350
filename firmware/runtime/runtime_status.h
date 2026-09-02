#ifndef RP86_RUNTIME_STATUS_H
#define RP86_RUNTIME_STATUS_H

#include "runtime/prepared_runtime.h"
#include "runtime/runtime_context.h"
#include "runtime/workload_executor.h"

typedef struct {
    rp86_workload_result_payload_t workload;
    bool physical_bus_active;
    bool prepared_runtime_available;
} rp86_runtime_status_snapshot_t;

void rp86_runtime_status_capture(
    rp86_runtime_status_snapshot_t *snapshot,
    const rp86_runtime_context_t *runtime,
    const rp86_workload_executor_t *executor,
    const rp86_prepared_runtime_t *prepared,
    rp86_workload_clock_mode_t physical_clock,
    bool physical_bus_active);

#endif
