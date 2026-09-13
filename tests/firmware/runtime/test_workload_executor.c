#include <assert.h>
#include <string.h>

#include "runtime/workload_executor.h"

/* Only hardware edges are stubbed; execute the production lifecycle code. */
static uint64_t now_us;
static unsigned safe_halts;
static bool completed_cycle;
static bool unmapped_cycle;

uint64_t time_us_64(void) { return now_us; }
void rp86_processor_bus_clear_fault(rp86_processor_bus_t *bus) {
    bus->faulted = false;
}
void rp86_processor_bus_force_safe_state(rp86_processor_bus_t *bus) {
    (void)bus;
    ++safe_halts;
}
void rp86_processor_bus_safe_halt(rp86_processor_bus_t *bus, uint clocks) {
    assert(clocks != 0u);
    rp86_processor_bus_force_safe_state(bus);
}
bool rp86_processor_bus_reset_sequence(rp86_processor_bus_t *bus, uint clocks) {
    (void)bus;
    return clocks != 0u;
}
void rp86_processor_bus_reset_step_timing(rp86_processor_bus_t *bus) {
    (void)bus;
}
bool rp86_clock_stepped_service_cycle(
    rp86_processor_bus_t *bus, rp86_memory_t *memory,
    const rp86_clock_stepped_io_t *io, uint max_idle_steps,
    rp86_clock_stepped_stats_t *stats) {
    (void)bus;
    (void)memory;
    (void)io;
    (void)max_idle_steps;
    stats->unmapped = unmapped_cycle;
    stats->no_cycle = !completed_cycle && !unmapped_cycle;
    if (completed_cycle) ++stats->cycles;
    return completed_cycle;
}
const char *rp86_clock_stepped_cycle_name(rp86_processor_bus_cycle_type_t type) {
    (void)type;
    return "TEST";
}
static bool handoff(void *context) { (void)context; return true; }

static rp86_host_protocol_message_t read_diagnostics(
    const rp86_workload_executor_t *executor, uint32_t id) {
    rp86_host_protocol_message_t request = {
        .version = RP86_HOST_PROTOCOL_VERSION,
        .type = RP86_HOST_PROTOCOL_MESSAGE_DIAGNOSTICS_REQUEST,
        .sequence = 37u, .length = 4u,
    };
    memcpy(request.payload, &id, sizeof id);
    rp86_host_protocol_message_t reply;
    assert(rp86_workload_executor_diagnostics(executor, &request, &reply));
    assert(reply.type == RP86_HOST_PROTOCOL_MESSAGE_DIAGNOSTICS_RESULT);
    assert(reply.sequence == 37u && reply.version == RP86_HOST_PROTOCOL_VERSION);
    return reply;
}

static void upload(rp86_runtime_context_t *runtime) {
    const uint8_t image[] = {0x90u};
    const rp86_workload_manifest_t manifest = {
        .magic = RP86_WORKLOAD_MAGIC,
        .version = RP86_WORKLOAD_FORMAT_VERSION,
        .header_size = sizeof(rp86_workload_manifest_t),
        .image_size = sizeof image,
        .image_crc32 = 0x220d7cc9u, /* CRC32 of 90h. */
        .load_address = 0x10000u,
        .entry_segment = 0x1000u,
        .flags = RP86_WORKLOAD_FLAG_CLOCK_STEPPED,
    };
    assert(rp86_workload_begin(&runtime->workload, 1u, &manifest));
    assert(rp86_workload_write(&runtime->workload, 1u, 0u, image, sizeof image));
    assert(rp86_workload_commit(&runtime->workload, 1u, manifest.image_crc32));
}

static rp86_workload_timeout_payload_t timeout_setting(
    rp86_workload_executor_t *executor, uint32_t operation, uint32_t milliseconds) {
    rp86_host_protocol_message_t request = {
        .version = RP86_HOST_PROTOCOL_VERSION,
        .type = RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_TIMEOUT_REQUEST,
        .sequence = 71u, .length = 8u,
    }, reply;
    const uint32_t fields[] = {operation, milliseconds};
    memcpy(request.payload, fields, sizeof fields);
    assert(rp86_workload_executor_timeout_request(executor, &request, &reply));
    assert(reply.status == 0u && reply.length == 52u && reply.sequence == 71u);
    assert(reply.type == RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_TIMEOUT_RESULT);
    rp86_workload_timeout_payload_t result;
    memcpy(&result, reply.payload, sizeof result);
    return result;
}

static void test_execution_deadline(void) {
    static uint8_t storage[0x40000u];
    rp86_runtime_context_t runtime = {0};
    rp86_processor_bus_t bus = {0};
    bool bus_active = false;
    uint32_t boot_id = 0u;
    rp86_workload_executor_t executor;
    now_us = 1000u;
    completed_cycle = true;
    unmapped_cycle = false;
    rp86_memory_backing_init_direct(&runtime.memory_backing, "TEST", 0u,
                                   storage, sizeof storage);
    rp86_workload_manager_init(&runtime.workload, &runtime.memory_backing);
    rp86_workload_executor_init(&executor, &runtime, &bus, &bus_active,
                               &boot_id, handoff, NULL, NULL, NULL);
    assert(timeout_setting(&executor, 0u, 0u).timeout_ms == 0u);
    upload(&runtime);
    rp86_workload_executor_stage(&executor);
    assert(timeout_setting(&executor, 1u, 1000u).armed == 0u);
    assert(rp86_workload_run(&runtime.workload, 0u));
    assert(rp86_workload_executor_start(&executor));
    now_us = 1000999u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING);
    assert(executor.bus_stats.cycles == 1u);
    const uint64_t deadline = executor.execution_deadline_us;
    assert(timeout_setting(&executor, 0u, 0u).remaining_ms == 1u);
    assert(executor.execution_deadline_us == deadline); /* GET is not a keepalive. */
    now_us = 1001000u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_TIMED_OUT);
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_EXECUTION_DEADLINE);
    assert(executor.bus_stats.cycles == 1u && !bus_active && !executor.active);
    assert(executor.clock_mode == RP86_WORKLOAD_CLOCK_STOPPED);
    assert(timeout_setting(&executor, 0u, 0u).armed == 0u);
    const rp86_host_protocol_message_t diagnostic = read_diagnostics(&executor, 0u);
    rp86_diagnostics_payload_t snapshot;
    memcpy(&snapshot, diagnostic.payload, sizeof snapshot);
    assert(snapshot.completion_reason == RP86_WORKLOAD_COMPLETION_EXECUTION_DEADLINE);

    /* New upload keeps policy, but gets a fresh deadline and clean result. */
    upload(&runtime);
    rp86_workload_executor_stage(&executor);
    assert(rp86_workload_run(&runtime.workload, 0u));
    assert(rp86_workload_executor_start(&executor));
    assert(timeout_setting(&executor, 0u, 0u).remaining_ms == 1000u);
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_NONE);
    assert(timeout_setting(&executor, 1u, 0u).armed == 0u);
    now_us += 3000000u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING); /* OFF */
    assert(timeout_setting(&executor, 1u, 5000u).remaining_ms == 2000u);
    assert(timeout_setting(&executor, 1u, 1000u).remaining_ms == 0u);
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_TIMED_OUT);

    upload(&runtime);
    rp86_workload_executor_stage(&executor);
    assert(rp86_workload_run(&runtime.workload, 0u));
    assert(rp86_workload_executor_start(&executor));
    executor.idle_armed = true;
    completed_cycle = false;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_COMPLETED);
    assert(timeout_setting(&executor, 0u, 0u).armed == 0u);
    now_us += 10000000u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_COMPLETED);
    assert(rp86_workload_restart(&runtime.workload, 0u));
    rp86_workload_executor_stop(&executor);
    assert(rp86_workload_executor_start(&executor));
    assert(timeout_setting(&executor, 0u, 0u).remaining_ms == 1000u);
    assert(rp86_workload_stop(&runtime.workload, 0u));
    rp86_workload_executor_stop(&executor);
    now_us += 10000000u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_STOPPED);

    rp86_host_protocol_message_t bad = {
        .version = RP86_HOST_PROTOCOL_VERSION,
        .type = RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_TIMEOUT_REQUEST,
        .length = 8u,
    }, reply;
    uint32_t fields[] = {1u, RP86_WORKLOAD_TIMEOUT_MAX_MS + 1u};
    memcpy(bad.payload, fields, sizeof fields);
    assert(rp86_workload_executor_timeout_request(&executor, &bad, &reply));
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH);
    assert(executor.timeout_ms == 1000u);
    fields[1] = 1000u;
    memcpy(bad.payload, fields, sizeof fields);
    runtime.workload.state = RP86_WORKLOAD_STATE_RUNNING; /* Prepared path */
    assert(rp86_workload_executor_timeout_request(&executor, &bad, &reply));
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_BAD_STATE);
    assert(timeout_setting(&executor, 1u, 0u).timeout_ms == 0u);
}

int main(void) {
    static uint8_t storage[0x40000u];
    rp86_runtime_context_t runtime = {0};
    rp86_processor_bus_t bus = {0};
    bool bus_active = false;
    uint32_t boot_id = 0u;
    rp86_workload_executor_t executor;
    rp86_memory_backing_init_direct(&runtime.memory_backing, "TEST", 0u,
                                   storage, sizeof storage);
    rp86_workload_manager_init(&runtime.workload, &runtime.memory_backing);
    rp86_workload_executor_init(&executor, &runtime, &bus, &bus_active,
                               &boot_id, handoff, NULL, NULL, NULL);
    upload(&runtime);
    rp86_workload_executor_stage(&executor);
    assert(read_diagnostics(&executor, 0u).status == RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE);

    /* run/restart without a new upload must discard the previous result. */
    executor.completion_reason = RP86_WORKLOAD_COMPLETION_STOP_REQUESTED;
    executor.result_flags = RP86_WORKLOAD_RESULT_PASS;
    memcpy(executor.native_output, "RESULT: PASS", 12u);
    executor.native_output_length = 12u;
    assert(rp86_workload_run(&runtime.workload, 0u));
    assert(rp86_workload_executor_start(&executor));
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_NONE);
    assert(executor.result_flags == 0u && executor.native_output_length == 0u);

    assert(read_diagnostics(&executor, 0u).status == RP86_HOST_PROTOCOL_STATUS_BAD_STATE);
    assert(executor.active && bus_active && safe_halts == 0u);

    /* A completed cycle cancels the starvation interval. */
    now_us = 1u;
    rp86_workload_executor_service(&executor);
    now_us = 90001u;
    completed_cycle = true;
    rp86_workload_executor_service(&executor);
    assert(executor.starvation_started_us == 0u);
    completed_cycle = false;
    now_us = 90002u;
    rp86_workload_executor_service(&executor);
    now_us = 190001u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING);
    assert(safe_halts == 0u);
    now_us = 190002u;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_TIMED_OUT);
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_NO_BUS_CYCLE);
    assert(!executor.active && !bus_active && safe_halts == 1u);
    assert(executor.clock_mode == RP86_WORKLOAD_CLOCK_STOPPED);
    assert(executor.starvation_started_us == 0u);

    /* The actual timed-out manager accepts a fresh verified image. */
    rp86_host_protocol_message_t reply = read_diagnostics(&executor, 0u);
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_OK && reply.length == 52u);
    rp86_diagnostics_payload_t snapshot;
    memcpy(&snapshot, reply.payload, sizeof snapshot);
    assert(snapshot.completion_reason == RP86_WORKLOAD_COMPLETION_NO_BUS_CYCLE);
    assert(snapshot.flags == RP86_DIAGNOSTICS_NO_CYCLE);
    assert(snapshot.boot_id == 1u && snapshot.last_address == 0u);
    assert(!rp86_workload_restart(&runtime.workload, 0u));
    upload(&runtime);
    rp86_workload_executor_stage(&executor);
    assert(rp86_workload_run(&runtime.workload, 0u));
    assert(rp86_workload_executor_start(&executor));
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_NONE);
    assert(executor.result_flags == 0u && executor.native_output_length == 0u);
    assert(executor.active && bus_active && boot_id == 2u);
    completed_cycle = true;
    rp86_workload_executor_service(&executor);
    assert(executor.bus_stats.cycles == 1u);

    /* Immediate faults must not be delayed or mislabeled as timeouts. */
    completed_cycle = false;
    unmapped_cycle = true;
    executor.bus_stats.first_cycle_seen = true;
    executor.bus_stats.last_address = 0x40000u;
    executor.bus_stats.last_type = RP86_PROCESSOR_BUS_CYCLE_MEM_READ;
    executor.bus_stats.last_lanes = RP86_PROCESSOR_BUS_LANES_WORD;
    executor.bus_stats.last_data = 0xbeefu;
    executor.bus_stats.last_data_valid = false;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_FAULTED);
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_BUS_FAULT);
    assert(!executor.active && !bus_active && safe_halts == 2u);

    reply = read_diagnostics(&executor, runtime.workload.workload_id);
    memcpy(&snapshot, reply.payload, sizeof snapshot);
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_OK);
    assert(snapshot.boot_id == 2u && snapshot.last_address == 0x40000u);
    assert(snapshot.cycle_type == RP86_PROCESSOR_BUS_CYCLE_MEM_READ);
    assert(snapshot.flags == (RP86_DIAGNOSTICS_CYCLE_VALID | RP86_DIAGNOSTICS_UNMAPPED));
    assert(snapshot.last_data == 0u); /* stale data is not exported as valid */
    assert(snapshot.reserved[0] == 0u && snapshot.reserved[1] == 0u && snapshot.reserved[2] == 0u);
    rp86_host_protocol_message_t again = read_diagnostics(&executor, 0u);
    assert(memcmp(&reply, &again, sizeof reply) == 0 && safe_halts == 2u);
    assert(read_diagnostics(&executor, runtime.workload.workload_id + 1u).status ==
           RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD);

    /* Beginning a new upload invalidates the old snapshot even if CRC fails. */
    rp86_workload_executor_clear_diagnostics(&executor);
    assert(read_diagnostics(&executor, 0u).status == RP86_HOST_PROTOCOL_STATUS_SERVICE_UNAVAILABLE);

    /* Even an early rejected start cannot retain a previous PASS. */
    executor.result_flags = RP86_WORKLOAD_RESULT_PASS;
    executor.native_output_length = 12u;
    runtime.workload.manifest.flags = RP86_WORKLOAD_FLAG_CLOCK_FREE_RUNNING;
    assert(!rp86_workload_executor_start(&executor));
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_START_FAILURE);
    assert(executor.result_flags == 0u && executor.native_output_length == 0u);
    assert(!executor.active && !bus_active);
    reply = read_diagnostics(&executor, 0u);
    memcpy(&snapshot, reply.payload, sizeof snapshot);
    assert(snapshot.completion_reason == RP86_WORKLOAD_COMPLETION_START_FAILURE);
    assert(snapshot.flags == 0u && snapshot.last_address == 0u && snapshot.boot_id == 0u);

    /* Malformed diagnostics requests always receive bounded error records. */
    rp86_host_protocol_message_t bad = {
        .version = RP86_HOST_PROTOCOL_VERSION,
        .type = RP86_HOST_PROTOCOL_MESSAGE_DIAGNOSTICS_REQUEST,
        .length = 53u,
    };
    assert(rp86_workload_executor_diagnostics(&executor, &bad, &reply));
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH && reply.length == 0u);
    bad.length = 4u;
    bad.version = 99u;
    assert(rp86_workload_executor_diagnostics(&executor, &bad, &reply));
    assert(reply.status == RP86_HOST_PROTOCOL_STATUS_BAD_VERSION);
    test_execution_deadline();
    return 0;
}
