#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "runtime/workload_executor.h"
#include "runtime/processor_abi.h"

static uint64_t now_us;
static bool intr_level;
static bool simulate_inta;
static bool simulate_eoi;
static bool simulate_terminal_arm;
static bool simulate_no_cycle;
static bool last_drive_vector;
static uint8_t last_vector;

uint64_t time_us_64(void) { return now_us; }

void rp86_processor_bus_set_intr(bool asserted) { intr_level = asserted; }
void rp86_processor_bus_clear_fault(rp86_processor_bus_t *bus) { bus->faulted = false; }
void rp86_processor_bus_force_safe_state(rp86_processor_bus_t *bus) { (void)bus; }
void rp86_processor_bus_safe_halt(rp86_processor_bus_t *bus, uint clocks) {
    (void)bus;
    assert(clocks != 0u);
    intr_level = false;
}
bool rp86_processor_bus_reset_sequence(rp86_processor_bus_t *bus, uint clocks) {
    (void)bus;
    return clocks != 0u;
}
void rp86_processor_bus_reset_step_timing(rp86_processor_bus_t *bus) { (void)bus; }

bool rp86_clock_stepped_service_cycle(
    rp86_processor_bus_t *bus, rp86_memory_t *memory,
    const rp86_clock_stepped_io_t *io, uint max_idle_steps,
    rp86_clock_stepped_stats_t *stats) {
    (void)bus;
    (void)memory;
    (void)max_idle_steps;
    stats->unmapped = false;
    stats->invalid_lane = false;
    stats->pad_mismatch = false;
    stats->clock_failure = false;
    stats->interrupt_ack = false;
    stats->no_cycle = false;

    if (simulate_no_cycle) {
        stats->no_cycle = true;
        return false;
    }
    if (simulate_inta) {
        bool drive = false;
        uint8_t vector = 0u;
        stats->interrupt_ack = true;
        assert(io != NULL && io->interrupt_ack != NULL);
        if (!io->interrupt_ack(io->context, &drive, &vector)) return false;
        last_drive_vector = drive;
        last_vector = vector;
        ++stats->interrupt_acks;
        ++stats->cycles;
        return true;
    }
    if (simulate_eoi) {
        assert(io != NULL && io->write != NULL);
        assert(io->write(io->context, RP86_IO_PORT_PIC_COMMAND,
                         RP86_PROCESSOR_BUS_LANE_LOW,
                         RP86_PIC_COMMAND_EOI));
        ++stats->io_writes;
        ++stats->cycles;
        return true;
    }
    if (simulate_terminal_arm) {
        assert(io != NULL && io->write != NULL);
        assert(io->write(io->context, RP86_IO_PORT_CONTROL,
                         RP86_PROCESSOR_BUS_LANE_LOW,
                         RP86_CONTROL_IDLE_PREPARE));
        ++stats->io_writes;
        ++stats->cycles;
        return true;
    }

    ++stats->cycles;
    return true;
}

const char *rp86_clock_stepped_cycle_name(rp86_processor_bus_cycle_type_t type) {
    (void)type;
    return "TEST";
}

static bool handoff(void *context) {
    (void)context;
    return true;
}

static void upload_tick_workload(rp86_runtime_context_t *runtime) {
    static const uint8_t image[] = {0x90u};
    const rp86_workload_manifest_t manifest = {
        .magic = RP86_WORKLOAD_MAGIC,
        .version = RP86_WORKLOAD_FORMAT_VERSION,
        .header_size = sizeof(rp86_workload_manifest_t),
        .image_size = sizeof image,
        .image_crc32 = 0x220d7cc9u,
        .load_address = 0x10000u,
        .entry_segment = 0x1000u,
        .flags = RP86_WORKLOAD_FLAG_CLOCK_STEPPED |
                 RP86_WORKLOAD_FLAG_PERIODIC_TICK,
    };
    assert(rp86_workload_begin(&runtime->workload, 1u, &manifest));
    assert(rp86_workload_write(&runtime->workload, 1u, 0u,
                               image, sizeof image));
    assert(rp86_workload_commit(&runtime->workload, 1u,
                                manifest.image_crc32));
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
    upload_tick_workload(&runtime);
    rp86_workload_executor_stage(&executor);
    assert(rp86_workload_run(&runtime.workload, 0u));

    now_us = 1000u;
    assert(rp86_workload_executor_start(&executor));
    assert(executor.tick_enabled);
    assert(executor.tick_next_us == 11000u);
    assert(!intr_level);

    /* Before the first period there is no request. */
    now_us = 10999u;
    rp86_workload_executor_service(&executor);
    assert(executor.tick_generated == 0u && !intr_level);

    /* At 10 ms exactly one request becomes pending and INTR is asserted. */
    now_us = 11000u;
    rp86_workload_executor_service(&executor);
    assert(executor.tick_generated == 1u);
    assert(executor.tick_pending && executor.tick_intr_asserted && intr_level);

    /* INTA #1 accepts the request but does not drive a vector. */
    simulate_inta = true;
    rp86_workload_executor_service(&executor);
    assert(!last_drive_vector && last_vector == 0u);
    assert(executor.tick_delivered == 1u);
    assert(executor.tick_ack_phase == 1u);
    assert(!executor.tick_pending && !executor.tick_intr_asserted && !intr_level);

    /* INTA #2 drives vector 21h and leaves the tick in service. */
    rp86_workload_executor_service(&executor);
    assert(last_drive_vector && last_vector == RP86_INTERRUPT_VECTOR_TICK);
    assert(executor.tick_acknowledged == 1u);
    assert(executor.tick_ack_phase == 0u && executor.tick_in_service);
    simulate_inta = false;

    /* EOI releases the in-service state. */
    simulate_eoi = true;
    rp86_workload_executor_service(&executor);
    simulate_eoi = false;
    assert(executor.tick_eoi == 1u && !executor.tick_in_service);

    /* Model a long CLI interval: INTR remains asserted and later wall-clock
     * periods collapse into the single pending request instead of queueing. */
    now_us = 21000u;
    rp86_workload_executor_service(&executor);
    assert(executor.tick_generated == 2u && intr_level && executor.tick_pending);
    now_us = 51000u;
    rp86_workload_executor_service(&executor);
    assert(executor.tick_generated == 5u);
    assert(executor.tick_coalesced == 3u);
    assert(executor.tick_pending && intr_level);

    simulate_inta = true;
    rp86_workload_executor_service(&executor); /* INTA #1 */
    rp86_workload_executor_service(&executor); /* INTA #2 */
    simulate_inta = false;
    assert(executor.tick_delivered == 2u);
    assert(executor.tick_acknowledged == 2u);
    assert(executor.tick_in_service);

    /* A period may elapse while the previous interrupt is still in service.
     * Retain exactly one request, then reassert it at the first complete bus
     * boundary after EOI even if the next 10 ms deadline has not arrived. */
    now_us = 61000u;
    rp86_workload_executor_service(&executor);
    assert(executor.tick_generated == 6u);
    assert(executor.tick_pending && executor.tick_in_service);
    assert(!executor.tick_intr_asserted && !intr_level);
    assert(executor.tick_delayed == 1u);

    simulate_eoi = true;
    rp86_workload_executor_service(&executor);
    simulate_eoi = false;
    assert(executor.tick_eoi == 2u && !executor.tick_in_service);
    assert(executor.tick_pending && !intr_level);

    rp86_workload_executor_service(&executor);
    assert(executor.tick_generated == 6u);
    assert(executor.tick_pending && executor.tick_intr_asserted && intr_level);

    simulate_inta = true;
    rp86_workload_executor_service(&executor); /* INTA #1 */
    rp86_workload_executor_service(&executor); /* INTA #2 */
    simulate_inta = false;
    assert(executor.tick_delivered == 3u);
    assert(executor.tick_acknowledged == 3u);
    assert(executor.tick_in_service);

    simulate_eoi = true;
    rp86_workload_executor_service(&executor);
    simulate_eoi = false;
    assert(executor.tick_eoi == 3u && !executor.tick_in_service);

    /* Terminal completion explicitly disables the periodic source. */
    simulate_terminal_arm = true;
    rp86_workload_executor_service(&executor);
    simulate_terminal_arm = false;
    assert(executor.idle_armed && !executor.tick_enabled && !intr_level);

    simulate_no_cycle = true;
    rp86_workload_executor_service(&executor);
    assert(runtime.workload.state == RP86_WORKLOAD_STATE_COMPLETED);
    assert(executor.completion_reason == RP86_WORKLOAD_COMPLETION_NATIVE_HLT);
    assert(executor.processor_idle);
    return 0;
}
