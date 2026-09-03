#ifndef RP86_WORKLOAD_EXECUTOR_H
#define RP86_WORKLOAD_EXECUTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bus/processor_bus.h"
#include "memory/memory.h"
#include "runtime/clock_stepped_bus_controller.h"
#include "runtime/runtime_context.h"

typedef bool (*rp86_workload_bus_handoff_fn)(void *context);
typedef void (*rp86_workload_evidence_fn)(void *context, const char *text);

typedef struct {
    uint32_t address;
    uint16_t data;
    uint8_t type;
    uint8_t lanes;
} rp86_workload_trace_entry_t;

enum {
    RP86_WORKLOAD_RESET_HANDOFF_SIZE = 16u,
    RP86_WORKLOAD_TRACE_DEPTH = 128u,
};

/*
 * Owns general native workload execution after the prepared diagnostic
 * responder hands off the physical processor bus.  PIO/DMA prepared-runtime
 * timing remains in its dedicated implementation; this object owns reset
 * handoff, clock-stepped execution, lifecycle completion, faults, and trace.
 */
typedef struct {
    rp86_runtime_context_t *runtime;
    rp86_processor_bus_t *processor_bus;
    bool *physical_bus_active;
    uint32_t *processor_boot_id;
    rp86_workload_bus_handoff_fn prepare_bus;
    void *prepare_bus_context;
    rp86_workload_evidence_fn evidence;
    void *evidence_context;
    rp86_memory_t processor_memory;
    rp86_clock_stepped_stats_t bus_stats;
    uint8_t reset_handoff[RP86_WORKLOAD_RESET_HANDOFF_SIZE];
    char diagnostic_line[96];
    uint32_t diagnostic_length;
    char native_output[RP86_WORKLOAD_RESULT_TEXT_BYTES];
    uint16_t native_output_length;
    uint32_t result_flags;
    rp86_workload_completion_reason_t completion_reason;
    bool active;
    bool idle_armed;
    bool processor_idle;
    uint64_t starvation_started_us;
    rp86_workload_clock_mode_t clock_mode;
    rp86_workload_trace_entry_t trace[RP86_WORKLOAD_TRACE_DEPTH];
    uint32_t trace_count;
    uint32_t diagnostic_workload_id;
    uint32_t diagnostic_boot_id;
} rp86_workload_executor_t;

void rp86_workload_executor_init(
    rp86_workload_executor_t *executor,
    rp86_runtime_context_t *runtime,
    rp86_processor_bus_t *processor_bus,
    bool *physical_bus_active,
    uint32_t *processor_boot_id,
    rp86_workload_bus_handoff_fn prepare_bus,
    void *prepare_bus_context,
    rp86_workload_evidence_fn evidence,
    void *evidence_context);

bool rp86_workload_executor_start(rp86_workload_executor_t *executor);
void rp86_workload_executor_stage(rp86_workload_executor_t *executor);
void rp86_workload_executor_clear_diagnostics(rp86_workload_executor_t *executor);
void rp86_workload_executor_stop(rp86_workload_executor_t *executor);
void rp86_workload_executor_service(rp86_workload_executor_t *executor);
bool rp86_workload_executor_diagnostics(
    const rp86_workload_executor_t *executor,
    const rp86_host_protocol_message_t *request,
    rp86_host_protocol_message_t *reply);

bool rp86_workload_executor_active(const rp86_workload_executor_t *executor);
bool rp86_workload_executor_processor_idle(
    const rp86_workload_executor_t *executor);
rp86_workload_clock_mode_t rp86_workload_executor_clock_mode(
    const rp86_workload_executor_t *executor);
const rp86_clock_stepped_stats_t *rp86_workload_executor_stats(
    const rp86_workload_executor_t *executor);
uint32_t rp86_workload_executor_result_flags(
    const rp86_workload_executor_t *executor);
rp86_workload_completion_reason_t rp86_workload_executor_completion_reason(
    const rp86_workload_executor_t *executor);
const char *rp86_workload_executor_native_output(
    const rp86_workload_executor_t *executor, uint16_t *length);

#endif
