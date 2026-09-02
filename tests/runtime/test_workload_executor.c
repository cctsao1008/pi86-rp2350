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

    /* run/restart without a new upload must discard the previous result. */
    executor.completion_reason = RP86_WORKLOAD_COMPLETION_STOP_REQUESTED;
    executor.result_flags = RP86_WORKLOAD_RESULT_PASS;
    memcpy(executor.native_output, "RESULT: PASS", 12u);
    executor.native_output_length = 12u;
    assert(rp86_workload_run(&runtime.workload, 0u));
    assert(rp86_workload_executor_start(&executor));
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_NONE);
    assert(executor.result_flags == 0u && executor.native_output_length == 0u);

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
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_FAULTED);
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_BUS_FAULT);
    assert(!executor.active && !bus_active && safe_halts == 2u);

    /* Even an early rejected start cannot retain a previous PASS. */
    executor.result_flags = RP86_WORKLOAD_RESULT_PASS;
    executor.native_output_length = 12u;
    runtime.workload.manifest.flags = RP86_WORKLOAD_FLAG_CLOCK_FREE_RUNNING;
    assert(!rp86_workload_executor_start(&executor));
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_START_FAILURE);
    assert(executor.result_flags == 0u && executor.native_output_length == 0u);
    assert(!executor.active && !bus_active);
    return 0;
}
