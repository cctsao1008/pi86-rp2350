#include "runtime/workload_executor.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "bus/prepared_responder.h"
#include "hardware/timer.h"
#include "memory/shared_mailbox.h"
#include "runtime/processor_abi.h"

enum {
    RESET_HANDOFF_BASE = 0xFFFF0u,
    GENERAL_BUS_STARVATION_TIMEOUT_US = 100000u,
};

static void emit(rp86_workload_executor_t *executor,
                 const char *format, ...) {
    if (executor->evidence == NULL) return;
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    executor->evidence(executor->evidence_context, text);
}

static bool build_reset_handoff(rp86_workload_executor_t *executor) {
    const rp86_workload_manifest_t *manifest =
        &executor->runtime->workload.manifest;
    memset(executor->reset_handoff, 0x90, sizeof executor->reset_handoff);
    uint32_t cursor = 0u;
    if (manifest->stack_segment != 0u || manifest->stack_offset != 0u) {
        executor->reset_handoff[cursor++] = 0xFAu; /* CLI */
        executor->reset_handoff[cursor++] = 0xB8u; /* MOV AX, imm16 */
        executor->reset_handoff[cursor++] = (uint8_t)manifest->stack_segment;
        executor->reset_handoff[cursor++] =
            (uint8_t)(manifest->stack_segment >> 8u);
        executor->reset_handoff[cursor++] = 0x8Eu; /* MOV SS, AX */
        executor->reset_handoff[cursor++] = 0xD0u;
        executor->reset_handoff[cursor++] = 0xBCu; /* MOV SP, imm16 */
        executor->reset_handoff[cursor++] = (uint8_t)manifest->stack_offset;
        executor->reset_handoff[cursor++] =
            (uint8_t)(manifest->stack_offset >> 8u);
    }
    if (cursor + 5u > sizeof executor->reset_handoff) return false;
    executor->reset_handoff[cursor++] = 0xEAu; /* JMP FAR entry */
    executor->reset_handoff[cursor++] = (uint8_t)manifest->entry_offset;
    executor->reset_handoff[cursor++] =
        (uint8_t)(manifest->entry_offset >> 8u);
    executor->reset_handoff[cursor++] = (uint8_t)manifest->entry_segment;
    executor->reset_handoff[cursor++] =
        (uint8_t)(manifest->entry_segment >> 8u);
    return true;
}

static void reset_native_result(rp86_workload_executor_t *executor) {
    memset(executor->native_output, 0, sizeof executor->native_output);
    executor->native_output_length = 0u;
    executor->result_flags = 0u;
    executor->completion_reason = RP86_WORKLOAD_COMPLETION_NONE;
}

static void flush_diagnostic_line(rp86_workload_executor_t *executor) {
    if (executor->diagnostic_length == 0u) return;
    executor->diagnostic_line[executor->diagnostic_length] = '\0';
    const uint32_t source_length = executor->diagnostic_length;
    const uint16_t retained = source_length < RP86_WORKLOAD_RESULT_TEXT_BYTES ?
        (uint16_t)source_length : RP86_WORKLOAD_RESULT_TEXT_BYTES;
    executor->result_flags &=
        ~(RP86_WORKLOAD_RESULT_PASS |
          RP86_WORKLOAD_RESULT_NATIVE_OUTPUT |
          RP86_WORKLOAD_RESULT_NATIVE_OUTPUT_TRUNCATED);
    memcpy(executor->native_output, executor->diagnostic_line, retained);
    executor->native_output_length = retained;
    executor->result_flags |= RP86_WORKLOAD_RESULT_NATIVE_OUTPUT;
    if (source_length > retained)
        executor->result_flags |= RP86_WORKLOAD_RESULT_NATIVE_OUTPUT_TRUNCATED;
    if (strcmp(executor->diagnostic_line, "RESULT: PASS") == 0)
        executor->result_flags |= RP86_WORKLOAD_RESULT_PASS;
    emit(executor, "[NATIVE STDOUT] %s\n", executor->diagnostic_line);
    executor->diagnostic_length = 0u;
}

static bool io_read(void *context, uint16_t port,
                    rp86_processor_bus_lanes_t lanes, uint16_t *value) {
    (void)context;
    (void)lanes;
    if (value == NULL) return false;
    if (port == RP86_IO_PORT_STATUS) {
        *value = 1u;
        return true;
    }
    return false;
}

static bool io_write(void *context, uint16_t port,
                     rp86_processor_bus_lanes_t lanes, uint16_t value) {
    rp86_workload_executor_t *executor = context;
    const uint16_t lane_value = lanes == RP86_PROCESSOR_BUS_LANE_HIGH ?
        (uint16_t)(value >> 8u) : value;
    if (port == RP86_IO_PORT_DIAGNOSTIC) {
        const char character = (char)(lane_value & 0xffu);
        if (character == '\r' || character == '\n') {
            flush_diagnostic_line(executor);
        } else if (executor->diagnostic_length + 1u <
                   sizeof executor->diagnostic_line) {
            executor->diagnostic_line[executor->diagnostic_length++] =
                character;
        }
        return true;
    }
    if (port == RP86_IO_PORT_RESULT) {
        emit(executor, "[NATIVE RESULT] %04X\n", lane_value);
        return true;
    }
    if (port == RP86_IO_PORT_EXECUTION_CLOCK) {
        if (lane_value == RP86_EXECUTION_CLOCK_REQUEST_FREE_RUNNING)
            return false;
        if (lane_value == RP86_EXECUTION_CLOCK_REQUEST_CLOCK_STEPPED)
            return executor->clock_mode == RP86_WORKLOAD_CLOCK_STEPPED;
    }
    if (port == RP86_IO_PORT_CONTROL &&
        lane_value == RP86_CONTROL_IDLE_PREPARE) {
        executor->idle_armed = true;
        return true;
    }
    return port == RP86_IO_PORT_STATUS || port == RP86_IO_PORT_TX ||
           port == RP86_IO_PORT_CONTROL || port == RP86_IO_PORT_RESULT ||
           port == RP86_IO_PORT_PIC_COMMAND;
}

static void retain_cycle(rp86_workload_executor_t *executor) {
    rp86_workload_trace_entry_t *entry =
        &executor->trace[executor->trace_count % RP86_WORKLOAD_TRACE_DEPTH];
    entry->address = executor->bus_stats.last_address;
    entry->data = executor->bus_stats.last_data;
    entry->type = (uint8_t)executor->bus_stats.last_type;
    entry->lanes = (uint8_t)executor->bus_stats.last_lanes;
    ++executor->trace_count;
}

static void print_trace(rp86_workload_executor_t *executor) {
    const uint32_t retained = executor->trace_count < 24u ?
        executor->trace_count : 24u;
    for (uint32_t i = retained; i != 0u; --i) {
        const uint32_t sequence = executor->trace_count - i;
        const rp86_workload_trace_entry_t *entry =
            &executor->trace[sequence % RP86_WORKLOAD_TRACE_DEPTH];
        emit(executor, "  trace[%lu] %s %05lX lanes=%u data=%04X\n",
             (unsigned long)sequence,
             rp86_clock_stepped_cycle_name(
                 (rp86_processor_bus_cycle_type_t)entry->type),
             (unsigned long)entry->address, entry->lanes, entry->data);
    }
}

void rp86_workload_executor_init(
    rp86_workload_executor_t *executor,
    rp86_runtime_context_t *runtime,
    rp86_processor_bus_t *processor_bus,
    bool *physical_bus_active,
    uint32_t *processor_boot_id,
    rp86_workload_bus_handoff_fn prepare_bus,
    void *prepare_bus_context,
    rp86_workload_evidence_fn evidence,
    void *evidence_context) {
    memset(executor, 0, sizeof *executor);
    executor->runtime = runtime;
    executor->processor_bus = processor_bus;
    executor->physical_bus_active = physical_bus_active;
    executor->processor_boot_id = processor_boot_id;
    executor->prepare_bus = prepare_bus;
    executor->prepare_bus_context = prepare_bus_context;
    executor->evidence = evidence;
    executor->evidence_context = evidence_context;
    executor->clock_mode = RP86_WORKLOAD_CLOCK_STOPPED;
}

bool rp86_workload_executor_start(rp86_workload_executor_t *executor) {
    /* A new attempt must never inherit a previous PASS/output/reason, even
     * when run follows stop or restart reuses the same staged image. */
    reset_native_result(executor);
    const rp86_workload_manifest_t *manifest =
        &executor->runtime->workload.manifest;
    if ((manifest->flags & RP86_WORKLOAD_FLAG_CLOCK_FREE_RUNNING) != 0u) {
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_START_FAILURE;
        return false;
    }
    if (!build_reset_handoff(executor)) {
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_START_FAILURE;
        return false;
    }
    if (executor->prepare_bus == NULL ||
        !executor->prepare_bus(executor->prepare_bus_context)) {
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_START_FAILURE;
        return false;
    }

    rp86_processor_bus_clear_fault(executor->processor_bus);
    rp86_memory_init(&executor->processor_memory,
                     (uint8_t *)executor->runtime->memory_backing.context,
                     executor->runtime->memory_backing.processor_base,
                     (uint32_t)executor->runtime->memory_backing.size,
                     executor->reset_handoff, RESET_HANDOFF_BASE,
                     sizeof executor->reset_handoff);
    if (!rp86_shared_mailbox_init(&executor->runtime->memory_backing)) {
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_START_FAILURE;
        rp86_processor_bus_force_safe_state(executor->processor_bus);
        return false;
    }
    memset(&executor->bus_stats, 0, sizeof executor->bus_stats);
    memset(executor->trace, 0, sizeof executor->trace);
    executor->trace_count = 0u;
    executor->diagnostic_length = 0u;
    executor->idle_armed = false;
    executor->processor_idle = false;
    executor->starvation_started_us = 0u;
    executor->clock_mode = RP86_WORKLOAD_CLOCK_STEPPED;
    executor->active = true;
    *executor->physical_bus_active = true;
    ++*executor->processor_boot_id;
    if (*executor->processor_boot_id == 0u) ++*executor->processor_boot_id;
    if (!rp86_processor_bus_reset_sequence(
            executor->processor_bus, RP86_PROCESSOR_RESET_CLOCKS)) {
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_START_FAILURE;
        executor->active = false;
        *executor->physical_bus_active = false;
        executor->clock_mode = RP86_WORKLOAD_CLOCK_STOPPED;
        rp86_processor_bus_force_safe_state(executor->processor_bus);
        return false;
    }
    rp86_processor_bus_reset_step_timing(executor->processor_bus);
    emit(executor,
         "[WORKLOAD START] id=%lu entry=%04X:%04X clock=CLOCK_STEPPED\n",
         (unsigned long)executor->runtime->workload.workload_id,
         manifest->entry_segment, manifest->entry_offset);
    return true;
}

void rp86_workload_executor_stage(rp86_workload_executor_t *executor) {
    memset(&executor->bus_stats, 0, sizeof executor->bus_stats);
    executor->processor_idle = false;
    reset_native_result(executor);
    const uint32_t flags = executor->runtime->workload.manifest.flags;
    executor->clock_mode =
        (flags & RP86_WORKLOAD_FLAG_CLOCK_FREE_RUNNING) != 0u ?
            RP86_WORKLOAD_CLOCK_FREE_RUNNING :
        (flags & RP86_WORKLOAD_FLAG_CLOCK_STEPPED) != 0u ?
            RP86_WORKLOAD_CLOCK_STEPPED : RP86_WORKLOAD_CLOCK_AUTO;
}

void rp86_workload_executor_stop(rp86_workload_executor_t *executor) {
    if (!executor->active) return;
    if (executor->completion_reason == RP86_WORKLOAD_COMPLETION_NONE)
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_STOP_REQUESTED;
    rp86_processor_bus_safe_halt(
        executor->processor_bus, RP86_PROCESSOR_RESET_CLOCKS);
    flush_diagnostic_line(executor);
    *executor->physical_bus_active = false;
    executor->idle_armed = false;
    executor->processor_idle = false;
    executor->starvation_started_us = 0u;
    executor->clock_mode = RP86_WORKLOAD_CLOCK_STOPPED;
    emit(executor, "[WORKLOAD STOP] cycles=%lu\n",
         (unsigned long)executor->bus_stats.cycles);
    executor->active = false;
}

void rp86_workload_executor_service(rp86_workload_executor_t *executor) {
    if (!executor->active ||
        executor->clock_mode != RP86_WORKLOAD_CLOCK_STEPPED ||
        executor->processor_idle)
        return;
    const rp86_clock_stepped_io_t io = {
        .context = executor,
        .read = io_read,
        .write = io_write,
    };
    const bool completed = rp86_clock_stepped_service_cycle(
        executor->processor_bus, &executor->processor_memory, &io, 32u,
        &executor->bus_stats);
    if (completed) {
        executor->starvation_started_us = 0u;
        retain_cycle(executor);
        return;
    }

    if (executor->idle_armed && !executor->bus_stats.pad_mismatch &&
        !executor->bus_stats.clock_failure &&
        !executor->bus_stats.interrupt_ack &&
        (executor->bus_stats.no_cycle || executor->bus_stats.unmapped ||
         executor->bus_stats.invalid_lane)) {
        executor->idle_armed = false;
        if (!rp86_workload_complete(
                &executor->runtime->workload,
                executor->runtime->workload.workload_id)) {
            emit(executor,
                 "[WORKLOAD FAULT] completion transition rejected\n");
            rp86_workload_executor_stop(executor);
            executor->runtime->workload.state = RP86_WORKLOAD_STATE_FAULTED;
            return;
        }
        executor->processor_idle = true;
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_NATIVE_HLT;
        executor->starvation_started_us = 0u;
        executor->bus_stats.unmapped = false;
        executor->bus_stats.invalid_lane = false;
        executor->bus_stats.no_cycle = false;
        emit(executor,
             "[WORKLOAD COMPLETED] armed native HLT indication accepted\n");
        return;
    }

    const bool immediate_fault = executor->bus_stats.unmapped ||
        executor->bus_stats.invalid_lane ||
        executor->bus_stats.pad_mismatch ||
        executor->bus_stats.clock_failure ||
        executor->bus_stats.interrupt_ack;
    if (executor->bus_stats.no_cycle && !immediate_fault) {
        const uint64_t now = time_us_64();
        if (executor->starvation_started_us == 0u)
            executor->starvation_started_us = now;
        if (now - executor->starvation_started_us <
            GENERAL_BUS_STARVATION_TIMEOUT_US)
            return;
        emit(executor,
             "[WORKLOAD TIMEOUT] no ALE for %lu us after cycle=%lu\n",
             (unsigned long)GENERAL_BUS_STARVATION_TIMEOUT_US,
             (unsigned long)executor->bus_stats.cycles);
        print_trace(executor);
        executor->completion_reason = RP86_WORKLOAD_COMPLETION_NO_BUS_CYCLE;
        rp86_workload_executor_stop(executor);
        executor->runtime->workload.state = RP86_WORKLOAD_STATE_TIMED_OUT;
        return;
    }

    executor->starvation_started_us = 0u;
    if (!immediate_fault) return;
    if (!executor->bus_stats.no_cycle) retain_cycle(executor);
    executor->completion_reason = RP86_WORKLOAD_COMPLETION_BUS_FAULT;
    emit(executor,
         "[WORKLOAD FAULT] cycle=%lu address=%05lX type=%s "
         "unmapped=%u lane=%u pad=%u clock=%u inta=%u\n",
         (unsigned long)executor->bus_stats.cycles,
         (unsigned long)executor->bus_stats.last_address,
         rp86_clock_stepped_cycle_name(executor->bus_stats.last_type),
         executor->bus_stats.unmapped,
         executor->bus_stats.invalid_lane,
         executor->bus_stats.pad_mismatch,
         executor->bus_stats.clock_failure,
         executor->bus_stats.interrupt_ack);
    print_trace(executor);
    rp86_workload_executor_stop(executor);
    executor->runtime->workload.state = RP86_WORKLOAD_STATE_FAULTED;
}

bool rp86_workload_executor_active(const rp86_workload_executor_t *executor) {
    return executor->active;
}

bool rp86_workload_executor_processor_idle(
    const rp86_workload_executor_t *executor) {
    return executor->processor_idle;
}

rp86_workload_clock_mode_t rp86_workload_executor_clock_mode(
    const rp86_workload_executor_t *executor) {
    return executor->clock_mode;
}

const rp86_clock_stepped_stats_t *rp86_workload_executor_stats(
    const rp86_workload_executor_t *executor) {
    return &executor->bus_stats;
}

uint32_t rp86_workload_executor_result_flags(
    const rp86_workload_executor_t *executor) {
    return executor->result_flags;
}

rp86_workload_completion_reason_t rp86_workload_executor_completion_reason(
    const rp86_workload_executor_t *executor) {
    return executor->completion_reason;
}

const char *rp86_workload_executor_native_output(
        const rp86_workload_executor_t *executor, uint16_t *length) {
    if (length != NULL) *length = executor->native_output_length;
    return executor->native_output;
}
