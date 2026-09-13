#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "runtime/workload_executor.h"
#include "runtime/processor_abi.h"

/*
 * Cross-layer transaction harness for issue #98.
 *
 * Unlike test_periodic_tick_executor.c, this test links the production
 * clock_stepped_bus_controller.c.  Only the lowest processor_bus_* primitives
 * are replaced, so rp86_workload_executor_service() reaches its real private
 * interrupt_ack/io_write callbacks through the real bus-controller dispatch.
 */

typedef enum {
    TEST_CYCLE_STATUS_READ,
    TEST_CYCLE_INTA,
    TEST_CYCLE_EOI,
} test_cycle_kind_t;

static uint64_t now_us;
static bool intr_level;
static test_cycle_kind_t next_cycle;
static uint16_t driven_value;
static rp86_processor_bus_lanes_t driven_lanes;
static unsigned inta_completions;
static bool last_inta_drive;
static uint8_t last_inta_vector;

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
bool rp86_processor_bus_faulted(const rp86_processor_bus_t *bus) {
    return bus == NULL || bus->faulted;
}

bool rp86_processor_bus_wait_cycle(rp86_processor_bus_t *bus,
                                   uint max_idle_steps,
                                   rp86_processor_bus_cycle_t *cycle) {
    (void)bus;
    assert(max_idle_steps != 0u);
    memset(cycle, 0, sizeof *cycle);
    switch (next_cycle) {
        case TEST_CYCLE_STATUS_READ:
            cycle->type = RP86_PROCESSOR_BUS_CYCLE_IO_READ;
            cycle->address = RP86_IO_PORT_STATUS;
            cycle->lanes = RP86_PROCESSOR_BUS_LANE_LOW;
            break;
        case TEST_CYCLE_INTA:
            cycle->type = RP86_PROCESSOR_BUS_CYCLE_INTERRUPT_ACK;
            cycle->lanes = RP86_PROCESSOR_BUS_LANES_NONE;
            break;
        case TEST_CYCLE_EOI:
            cycle->type = RP86_PROCESSOR_BUS_CYCLE_IO_WRITE;
            cycle->address = RP86_IO_PORT_PIC_COMMAND;
            cycle->lanes = RP86_PROCESSOR_BUS_LANE_LOW;
            break;
    }
    return true;
}

void rp86_processor_bus_drive_data(uint16_t value,
                                   rp86_processor_bus_lanes_t lanes) {
    driven_value = value;
    driven_lanes = lanes;
}

bool rp86_processor_bus_complete_read(rp86_processor_bus_t *bus,
                                      uint16_t *readback1,
                                      uint16_t *readback2) {
    (void)bus;
    assert(driven_lanes == RP86_PROCESSOR_BUS_LANE_LOW);
    if (readback1 != NULL) *readback1 = driven_value;
    if (readback2 != NULL) *readback2 = driven_value;
    return true;
}

bool rp86_processor_bus_complete_write(
    rp86_processor_bus_t *bus,
    const rp86_processor_bus_cycle_t *cycle,
    uint16_t *sample0, uint16_t *sample1, uint16_t *sample2) {
    (void)bus;
    assert(cycle->type == RP86_PROCESSOR_BUS_CYCLE_IO_WRITE);
    if (sample0 != NULL) *sample0 = RP86_PIC_COMMAND_EOI;
    if (sample1 != NULL) *sample1 = RP86_PIC_COMMAND_EOI;
    if (sample2 != NULL) *sample2 = RP86_PIC_COMMAND_EOI;
    return true;
}

bool rp86_processor_bus_complete_interrupt_ack(
    rp86_processor_bus_t *bus, bool drive_vector, uint8_t vector) {
    (void)bus;
    ++inta_completions;
    last_inta_drive = drive_vector;
    last_inta_vector = vector;
    return true;
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

static void assert_tick_invariants(const rp86_workload_executor_t *executor) {
    if (executor->tick_intr_asserted) {
        assert(executor->tick_pending);
        assert(executor->tick_ack_phase == 0u);
        assert(!executor->tick_in_service);
        assert(intr_level);
    }
    if (executor->tick_ack_phase != 0u) {
        assert(executor->tick_ack_phase == 1u);
        assert(!executor->tick_pending);
        assert(!executor->tick_intr_asserted);
        assert(!executor->tick_in_service);
        assert(!intr_level);
    }
    if (executor->tick_in_service) {
        assert(executor->tick_ack_phase == 0u);
        assert(!executor->tick_intr_asserted);
        assert(!intr_level);
    }
    if (intr_level) assert(executor->tick_intr_asserted);
}

static void service(rp86_workload_executor_t *executor,
                    test_cycle_kind_t kind) {
    next_cycle = kind;
    rp86_workload_executor_service(executor);
    assert_tick_invariants(executor);
}

static void advance_status_cycles(rp86_workload_executor_t *executor,
                                  uint32_t target) {
    while (executor->bus_stats.cycles < target)
        service(executor, TEST_CYCLE_STATUS_READ);
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
    assert_tick_invariants(&executor);

    /* Production composition: update_periodic_tick() asserts INTR before the
     * real controller services the ordinary status-read bus cycle. */
    now_us = 11000u;
    service(&executor, TEST_CYCLE_STATUS_READ);
    assert(executor.tick_generated == 1u);
    assert(executor.tick_pending && executor.tick_intr_asserted && intr_level);

    /* Real controller -> private executor INTA #1 policy -> physical-completion
     * stub.  The callback must consume pending/retractable state atomically
     * within this one service transaction. */
    service(&executor, TEST_CYCLE_INTA);
    assert(inta_completions == 1u);
    assert(!last_inta_drive && last_inta_vector == 0u);
    assert(executor.tick_ack_phase == 1u);
    assert(!executor.tick_pending && !executor.tick_intr_asserted && !intr_level);

    /* INTA #2 enters in-service state and drives vector 21h. */
    service(&executor, TEST_CYCLE_INTA);
    assert(inta_completions == 2u);
    assert(last_inta_drive && last_inta_vector == RP86_INTERRUPT_VECTOR_TICK);
    assert(executor.tick_ack_phase == 0u && executor.tick_in_service);

    /* The real controller's IO-write path reaches the private executor EOI
     * callback before committing the current cycle. */
    now_us = 11500u;
    service(&executor, TEST_CYCLE_EOI);
    assert(executor.tick_eoi == 1u && !executor.tick_in_service);
    assert(executor.tick_delivery_not_before_us == 21500u);
    const uint32_t real_eoi_progress_deadline =
        executor.tick_delivery_not_before_cycle;
    assert(real_eoi_progress_deadline == executor.bus_stats.cycles + 64u);

    /* The next phase-locked source event becomes pending at 21 ms, but cannot
     * assert until both recovery gates are open. */
    now_us = 21000u;
    service(&executor, TEST_CYCLE_STATUS_READ);
    assert(executor.tick_generated == 2u);
    assert(executor.tick_pending && !executor.tick_intr_asserted && !intr_level);

    advance_status_cycles(&executor, real_eoi_progress_deadline);
    now_us = 21499u;
    service(&executor, TEST_CYCLE_STATUS_READ);
    assert(executor.tick_pending && !executor.tick_intr_asserted && !intr_level);

    now_us = 21500u;
    service(&executor, TEST_CYCLE_STATUS_READ);
    assert(executor.tick_pending && executor.tick_intr_asserted && intr_level);

    /* Legal competing ordering: a voluntary-yield EOI bus cycle wins before
     * INTA #1 is accepted.  The request is retracted but must remain pending. */
    const uint64_t wall_gate_before_yield = executor.tick_delivery_not_before_us;
    service(&executor, TEST_CYCLE_EOI);
    assert(executor.tick_eoi == 1u); /* yield fence is not a real tick EOI */
    assert(executor.tick_pending);
    assert(!executor.tick_intr_asserted && !intr_level);
    assert(executor.tick_delivery_not_before_us == wall_gate_before_yield);
    const uint32_t yield_progress_deadline =
        executor.tick_delivery_not_before_cycle;
    assert(yield_progress_deadline == executor.bus_stats.cycles + 64u);

    /* After the refreshed processor-progress gate, the retained request may be
     * reasserted and then accepted by the normal two-cycle INTA sequence. */
    advance_status_cycles(&executor, yield_progress_deadline);
    service(&executor, TEST_CYCLE_STATUS_READ);
    assert(executor.tick_pending && executor.tick_intr_asserted && intr_level);

    service(&executor, TEST_CYCLE_INTA);
    assert(executor.tick_ack_phase == 1u);
    assert(!executor.tick_pending && !executor.tick_intr_asserted && !intr_level);

    service(&executor, TEST_CYCLE_INTA);
    assert(executor.tick_in_service);
    assert(last_inta_drive && last_inta_vector == RP86_INTERRUPT_VECTOR_TICK);

    /* Once INTA #1 has won the serialized bus ordering, there is no pending
     * request left for the yield-retraction rule to consume. */
    assert(!executor.tick_pending);
    assert(!executor.tick_intr_asserted);

    return 0;
}
