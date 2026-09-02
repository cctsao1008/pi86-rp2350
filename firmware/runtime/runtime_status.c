#include "runtime/runtime_status.h"

#include <string.h>

void rp86_runtime_status_capture(
        rp86_runtime_status_snapshot_t *snapshot,
        const rp86_runtime_context_t *runtime,
        const rp86_workload_executor_t *executor,
        const rp86_prepared_runtime_t *prepared,
        rp86_workload_clock_mode_t physical_clock,
        bool physical_bus_active) {
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->workload.status.workload_id = runtime->workload.workload_id;
    snapshot->workload.status.state = (uint32_t)runtime->workload.state;
    snapshot->workload.status.detail = runtime->workload.received;
    snapshot->workload.status.clock_mode = (uint32_t)physical_clock;
    snapshot->workload.status.processor_cycles =
        rp86_workload_executor_stats(executor)->cycles;
    snapshot->workload.status.processor_flags =
        (rp86_workload_executor_processor_idle(executor) ?
            RP86_WORKLOAD_PROCESSOR_IDLE : 0u) |
        (rp86_prepared_runtime_initialized(prepared) ?
            RP86_WORKLOAD_PREPARED_RUNTIME_INITIALIZED : 0u);
    snapshot->workload.result_flags =
        rp86_workload_executor_result_flags(executor);
    snapshot->workload.completion_reason =
        (uint32_t)rp86_workload_executor_completion_reason(executor);
    snapshot->workload.processor_signature = prepared->processor_signature;
    if (prepared->processor_identity_valid)
        snapshot->workload.result_flags |=
            RP86_WORKLOAD_RESULT_PROCESSOR_IDENTIFIED;
    uint16_t native_output_length = 0u;
    const char *native_output = rp86_workload_executor_native_output(
        executor, &native_output_length);
    snapshot->workload.native_output_length = native_output_length;
    memcpy(snapshot->workload.native_output, native_output,
           snapshot->workload.native_output_length);
    snapshot->physical_bus_active = physical_bus_active;
    snapshot->prepared_runtime_available =
        rp86_prepared_runtime_available(prepared);
}
