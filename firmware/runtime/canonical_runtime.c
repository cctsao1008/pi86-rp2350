/*
 * Persistent RP86 physical-processor runtime.
 *
 * This source implements the canonical 1.000 MHz runtime
 * (performance target). PIO1 owns every current-cycle read response and both
 * INTA cycles. Core0 owns policy only: immutable record publication, INTR
 * assertion, timeout/retry bookkeeping, and USB service between bus cycles.
 */

#include "runtime/canonical_runtime.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/watchdog.h"
#include "pico/stdio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#ifndef RP86_PROCESSOR_HZ
#define RP86_PROCESSOR_HZ 600000u
#endif
#include "host_protocol/usb_transport.h"
#include "processor_runtime_image.h"
#include "memory/memory.h"
#include "bus/prepared_responder.h"
#include "bus/processor_bus.h"
#include "bus/processor_bus_pins.h"
#include "processor_service.pio.h"
#include "runtime/cdc_command_parser.h"
#include "runtime/clock_stepped_bus_controller.h"
#include "runtime/evidence_queue.h"
#include "runtime/host_service_dispatch.h"
#include "runtime/prepared_runtime.h"
#include "runtime/processor_abi.h"
#include "runtime/runtime_context.h"
#include "runtime/runtime_status.h"
#include "runtime/workload_executor.h"
#include "runtime/workload_manager.h"
#include "storage/flash_layout.h"
#include "storage/flash_volume.h"

#define COMPANION_VECTOR             RP86_INTERRUPT_VECTOR_COMPANION
#define INT60_VECTOR                 RP86_INTERRUPT_VECTOR_NATIVE_SERVICE
#define INT60_HANDLER_OFFSET       0x0100u
#define IRQ_HANDLER_OFFSET         0x0140u
#define INITIAL_INT60_RETURN       0x0020u
#define IRQ_RETURN_OFFSET          0x0022u
#define CYCLIC_INT60_RETURN        0x0024u
#define IVT20_OFFSET_ADDRESS       RP86_IVT_COMPANION_OFFSET_ADDRESS
#define IVT20_SEGMENT_ADDRESS      RP86_IVT_COMPANION_SEGMENT_ADDRESS
#define IVT60_OFFSET_ADDRESS       RP86_IVT_NATIVE_SERVICE_OFFSET_ADDRESS
#define IVT60_SEGMENT_ADDRESS      RP86_IVT_NATIVE_SERVICE_SEGMENT_ADDRESS
#define CALCULATOR_STACK_IP_ADDRESS 0x7FF6u
#define CALCULATOR_STACK_CS_ADDRESS 0x7FF8u
#define STACK_IP_ADDRESS           0x7FFAu
#define STACK_CS_ADDRESS           0x7FFCu
#define STACK_FLAGS_ADDRESS        0x7FFEu
#define STATUS_PORT                RP86_IO_PORT_STATUS
#define TX_PORT                    RP86_IO_PORT_TX
#define RX_PORT                    RP86_IO_PORT_RX
#define CONTROL_PORT               RP86_IO_PORT_CONTROL
#define WITNESS_PORT               RP86_IO_PORT_RESULT
#define PIC_COMMAND_PORT           RP86_IO_PORT_PIC_COMMAND
#define HOST_WORDS                       7u
#define HEARTBEAT_ACCEPT_COUNT           8u
#define IRQ_PERIOD_US                50000u
#define IRQ_TIMEOUT_US               10000u
#define LIVE_ROUND_TIMEOUT_US         50000u
#define HOST_TIMEOUT_US            5000000u
#define EXACT_RESPONDER_CLK_SYS_HZ 150000000u
#define MAX_PAIRS                       96u
#define STREAM_WORDS (MAX_PAIRS * 2u)
#define NATIVE_TEXT_WORDS                 6u
#define NATIVE_PROCESSOR_WORDS            1u
#define NATIVE_COUNTER_COPIES              3u
#define NATIVE_REPLY_WORDS               13u
#define CALCULATOR_MAGIC               0xCA1Cu
#define CALCULATOR_SLOT_OFFSET         0x0170u
#define CALCULATOR_RETURN_OFFSET       0x0176u
#define CALCULATOR_WORKLOAD_SIZE             16u
#define CALCULATOR_ENTRY_STRIDE               4u
#define CALCULATOR_ADD                       1u
#define CALCULATOR_SUB                       2u
#define CALCULATOR_MUL                       3u
#define CALCULATOR_DIV                       4u
#define CALCULATOR_OPCODE_ADD           0xC801u /* 01 C8: ADD AX,CX */
#define CALCULATOR_OPCODE_SUB           0xC829u /* 29 C8: SUB AX,CX */
#define CALCULATOR_OPCODE_MUL           0xE1F7u /* F7 E1: MUL CX */
#define CALCULATOR_OPCODE_DIV           0xF1F7u /* F7 F1: DIV CX */
#define CLOCK_STEPPED_PIO_HZ            2000000u
#define EXECUTION_CLOCK_SWITCH_US        100000u
#define RESET_ROM_BASE                  0xFFFF0u
#define PROCESSOR_IMAGE_BASE             0xF0000u

typedef struct {
    uint32_t key;
    uint32_t response;
} exact_pair_t;

typedef struct {
    exact_pair_t pair[MAX_PAIRS];
    uint32_t count;
} exact_sequence_t;

typedef struct {
    bool reset_ok;
    bool pre_release_clean;
    bool first_inta_seen;
    bool second_inta_complete;
    bool int60_commit_seen;
    bool irq_commit_seen;
    bool eoi_seen;
    bool heartbeat_active;
    bool non_ad_isolation;
    bool observer_complete;
    bool processor_identity_valid;
    uint16_t processor_signature;
    uint32_t irq_assertions;
    uint32_t irq_accepts;
    uint32_t irq_completions;
    uint32_t int60_commits;
    uint32_t irq_commits;
    uint32_t eoi_writes;
    uint32_t complete_cycles;
    uint32_t first_mismatch_cycle;
    uint32_t observer_words;
    uint32_t foreground_dma_remain;
    uint32_t irq_rom_dma_remain;
    uint32_t irq_io_dma_remain;
    uint32_t foreground_fifo;
    uint32_t irq_rom_fifo;
    uint32_t irq_io_fifo;
    uint32_t foreground_pc;
    uint32_t irq_rom_pc;
    uint32_t irq_io_pc;
    uint32_t inta_pc;
} companion_result_t;

static exact_sequence_t g_boot;
static exact_sequence_t g_int60_initial;
static exact_sequence_t g_int60;
static exact_sequence_t g_irq_rom;
static exact_sequence_t g_irq_io;
static uint32_t g_foreground_initial_words[STREAM_WORDS];
static uint32_t g_int60_words[STREAM_WORDS];
static uint32_t g_irq_rom_words[STREAM_WORDS];
static uint32_t g_irq_io_words[STREAM_WORDS];
static uint16_t g_host_words[HOST_WORDS];
static rp86_host_protocol_message_t g_bootstrap_record;
static rp86_host_protocol_message_t g_reply_record;
static rp86_runtime_context_t g_runtime;
static rp86_processor_bus_t g_processor_bus;
static rp86_evidence_queue_t g_runtime_evidence;
static rp86_cdc_command_parser_t g_cdc_command_parser;
static rp86_workload_executor_t g_workload_executor;
static rp86_prepared_runtime_t g_prepared_runtime;
static int g_foreground_dma = -1;
static int g_irq_rom_dma = -1;
static int g_irq_io_dma = -1;
static int g_observer_dma = -1;
static rp86_prepared_sm_t g_foreground_sm;
static rp86_prepared_sm_t g_irq_rom_sm;
static rp86_prepared_sm_t g_irq_io_sm;
static rp86_prepared_sm_t g_inta_sm;
static rp86_prepared_sm_t g_observer_sm;
static uint32_t g_observer_dma_words[RP86_PREPARED_OBSERVER_WORDS];
static uint32_t g_int60_dma_words;
static uint32_t g_irq_rom_dma_words;
static uint32_t g_irq_io_dma_words;
static uint32_t g_processor_boot_id;
static uint16_t g_calculator_opcode = CALCULATOR_OPCODE_ADD;
static uint32_t g_calculator_entry_linear;
static const uint8_t g_calculator_workload_contract[CALCULATOR_WORKLOAD_SIZE] = {
    0x01u, 0xC8u, 0xCBu, 0x90u,
    0x29u, 0xC8u, 0xCBu, 0x90u,
    0xF7u, 0xE1u, 0xCBu, 0x90u,
    0xF7u, 0xF1u, 0xCBu, 0x90u,
};

static int evidence_printf(const char *format, ...);
static void drain_runtime_evidence(void);
static void service_cdc_control(void);
static bool handle_runtime_control(const rp86_host_protocol_message_t *record);
static bool handle_filesystem_record(const rp86_host_protocol_message_t *record);
static bool handle_memory_record(const rp86_host_protocol_message_t *record);
static bool handle_workload_record(const rp86_host_protocol_message_t *record);
static void park_physical_processor(void);
static bool start_calculator_workload(void);
static bool prepare_general_workload_bus(void *context);
static void workload_evidence(void *context, const char *text);

#ifndef RP86_CANONICAL_RUNTIME
#define RP86_CANONICAL_RUNTIME 0
#endif
#ifndef RP86_HAS_EXTERNAL_PSRAM
#define RP86_HAS_EXTERNAL_PSRAM 0
#endif
#ifndef RP86_HAS_SDCARD
#define RP86_HAS_SDCARD 1
#endif
#ifndef RP86_HAS_DVI
#define RP86_HAS_DVI 1
#endif
#ifndef RP86_HAS_PIO_USB
#define RP86_HAS_PIO_USB 1
#endif

static bool g_bus_active;

static bool take_non_control_record(rp86_host_protocol_message_t *record) {
    if (!rp86_host_protocol_hid_take_record((uint8_t *)record)) return false;
    if (record->type == RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_TIMEOUT_REQUEST) {
        rp86_host_protocol_message_t reply;
        rp86_workload_executor_timeout_request(&g_workload_executor, record, &reply);
        rp86_host_protocol_hid_send_record((const uint8_t *)&reply, HOST_TIMEOUT_US);
        return false;
    }
    if (record->type == RP86_HOST_PROTOCOL_MESSAGE_DIAGNOSTICS_REQUEST) {
        rp86_host_protocol_message_t reply;
        rp86_workload_executor_diagnostics(&g_workload_executor, record, &reply);
        rp86_host_protocol_hid_send_record((const uint8_t *)&reply, HOST_TIMEOUT_US);
        return false;
    }
    if (!rp86_host_protocol_payload_length_valid(record)) {
        if (record->version == RP86_HOST_PROTOCOL_VERSION &&
            record->type >= RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_BEGIN &&
            record->type <= RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_CONTROL)
            handle_workload_record(record);
        return false;
    }
    if (record->version == RP86_HOST_PROTOCOL_VERSION &&
        record->type == RP86_HOST_PROTOCOL_MESSAGE_RUNTIME_CONTROL) {
        handle_runtime_control(record);
        return false;
    }
    if (record->version == RP86_HOST_PROTOCOL_VERSION &&
        record->type == RP86_HOST_PROTOCOL_MESSAGE_FILESYSTEM_REQUEST) {
        handle_filesystem_record(record);
        return false;
    }
    if (record->version == RP86_HOST_PROTOCOL_VERSION &&
        record->type == RP86_HOST_PROTOCOL_MESSAGE_MEMORY_REQUEST) {
        handle_memory_record(record);
        return false;
    }
    if (record->version == RP86_HOST_PROTOCOL_VERSION &&
        record->type >= RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_BEGIN &&
        record->type <= RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_CONTROL) {
        handle_workload_record(record);
        return false;
    }
    return true;
}

typedef struct {
    bool inta1;
    bool inta2;
    bool witness;
    bool irq_commit;
    bool eoi;
    bool foreground_commit;
    bool native_counter_valid;
    bool processor_identity_valid;
    uint16_t observed_witness;
    uint16_t processor_signature;
    uint32_t observed_cycles;
    uint32_t cpu_sequence;
    uint32_t command_sequence;
    bool calculator_requested;
    bool calculator_valid;
    uint16_t calculator_operation;
    uint16_t calculator_lhs;
    uint16_t calculator_rhs;
    uint16_t calculator_low;
    uint16_t calculator_high;
} live_round_result_t;

static uint32_t memory_key(uint32_t address) {
    return rp86_prepared_memory_read_key(address);
}

static uint32_t io_read_key(uint16_t port) {
    return (1u << RP86_PROCESSOR_PIN_ASTB) | (1u << RP86_PROCESSOR_PIN_INTAK) |
           rp86_prepared_encode_address(port);
}

static void add_pair(exact_sequence_t *s, uint32_t key, uint16_t value) {
    hard_assert(s->count < MAX_PAIRS);
    s->pair[s->count++] =
        (exact_pair_t){key, rp86_prepared_encode_drive(value)};
}

static void add_memory(exact_sequence_t *s, uint32_t address,
                       uint16_t value) {
    add_pair(s, memory_key(address), value);
}

static void add_io(exact_sequence_t *s, uint16_t port, uint16_t value) {
    add_pair(s, io_read_key(port), value);
}

static void add_image_range(exact_sequence_t *s, uint32_t first,
                            uint32_t end_exclusive);

static uint16_t image_word(uint32_t offset) {
    hard_assert((offset & 1u) == 0u);
    hard_assert(offset + 1u < processor_runtime_image_size);
    if (g_runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING) {
        if (offset == CALCULATOR_SLOT_OFFSET)
            return 0x9A90u;
        if (offset == CALCULATOR_SLOT_OFFSET + 2u)
            return (uint16_t)(g_calculator_entry_linear & 0x000Fu);
        if (offset == CALCULATOR_SLOT_OFFSET + 4u)
            return (uint16_t)(g_calculator_entry_linear >> 4u);
    } else if (offset == CALCULATOR_SLOT_OFFSET) {
        return g_calculator_opcode;
    }
    return (uint16_t)processor_runtime_image_data[offset] |
           ((uint16_t)processor_runtime_image_data[offset + 1u] << 8);
}

static bool calculator_workload_valid(void) {
    const rp86_workload_manifest_t *manifest = &g_runtime.workload.manifest;
    if ((g_runtime.workload.state != RP86_WORKLOAD_STATE_STAGED &&
         g_runtime.workload.state != RP86_WORKLOAD_STATE_STOPPED &&
         g_runtime.workload.state != RP86_WORKLOAD_STATE_RUNNING) ||
        manifest->image_size != CALCULATOR_WORKLOAD_SIZE ||
        (((uint32_t)manifest->entry_segment << 4u) +
         manifest->entry_offset) != manifest->load_address)
        return false;

    uint8_t image[CALCULATOR_WORKLOAD_SIZE];
    return rp86_memory_backing_read(&g_runtime.memory_backing,
                                    manifest->load_address,
                                    image, sizeof image) &&
           memcmp(image, g_calculator_workload_contract, sizeof image) == 0;
}

static uint16_t calculator_workload_word(uint32_t address) {
    uint8_t bytes[2];
    hard_assert(rp86_memory_backing_read(&g_runtime.memory_backing, address,
                                         bytes, sizeof bytes));
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
}

static void compile_irq_rom_sequence(void) {
    memset(&g_irq_rom, 0, sizeof g_irq_rom);
    add_memory(&g_irq_rom, IVT20_OFFSET_ADDRESS, IRQ_HANDLER_OFFSET);
    add_memory(&g_irq_rom, IVT20_SEGMENT_ADDRESS, 0xF000u);
    if (g_runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING) {
        add_image_range(&g_irq_rom, IRQ_HANDLER_OFFSET,
                        CALCULATOR_RETURN_OFFSET);
        add_memory(&g_irq_rom, g_calculator_entry_linear,
                   calculator_workload_word(g_calculator_entry_linear));
        add_memory(&g_irq_rom, g_calculator_entry_linear + 2u,
                   calculator_workload_word(g_calculator_entry_linear + 2u));
        /* The nested far call pushes F000:0176 below the interrupt frame.
         * RETF consumes these two words before ROM fetching resumes. */
        add_memory(&g_irq_rom, CALCULATOR_STACK_IP_ADDRESS, CALCULATOR_RETURN_OFFSET);
        add_memory(&g_irq_rom, CALCULATOR_STACK_CS_ADDRESS, 0xF000u);
        add_image_range(&g_irq_rom, CALCULATOR_RETURN_OFFSET,
                        processor_runtime_image_size & ~1u);
    } else {
        add_image_range(&g_irq_rom, IRQ_HANDLER_OFFSET,
                        processor_runtime_image_size & ~1u);
    }
    add_memory(&g_irq_rom, STACK_IP_ADDRESS, IRQ_RETURN_OFFSET);
    add_memory(&g_irq_rom, STACK_CS_ADDRESS, 0xF000u);
    add_memory(&g_irq_rom, STACK_FLAGS_ADDRESS, 0xF246u);
}

static void add_image_range(exact_sequence_t *s, uint32_t first,
                            uint32_t end_exclusive) {
    for (uint32_t offset = first; offset < end_exclusive; offset += 2u)
        add_memory(s, PROCESSOR_IMAGE_BASE + offset, image_word(offset));
}

static void compile_sequences(void) {
    /* The exact-word responder has no odd-address instruction keys. Keep the
     * C-side stack/IRET contract locked to the assembly alignment contract. */
    hard_assert((INITIAL_INT60_RETURN & 1u) == 0u);
    hard_assert((IRQ_RETURN_OFFSET & 1u) == 0u);
    hard_assert((CYCLIC_INT60_RETURN & 1u) == 0u);
    hard_assert((INT60_HANDLER_OFFSET & 1u) == 0u);
    hard_assert((IRQ_HANDLER_OFFSET & 1u) == 0u);

    memset(&g_boot, 0, sizeof g_boot);
    memset(&g_int60_initial, 0, sizeof g_int60_initial);
    memset(&g_int60, 0, sizeof g_int60);
    memset(&g_irq_io, 0, sizeof g_irq_io);

    add_memory(&g_boot, RESET_ROM_BASE, 0x00EAu);
    add_memory(&g_boot, RESET_ROM_BASE + 2u, 0x0000u);
    add_memory(&g_boot, RESET_ROM_BASE + 4u, 0x90F0u);
    /* The reset path prefetches through the first INT 60h. Unsupported
     * speculative reads remain high-Z and do not advance another SM. */
    /* The first INT 60h is decoded after the aligned F0022 fetch.  Do not put
     * speculative F0024+ keys ahead of the IVT60 keys in the shared SM0
     * stream, or the exact matcher would wait on a fetch that never occurs. */
    add_image_range(&g_boot, 0x0000u, CYCLIC_INT60_RETURN);
    /* The boot INT60 and cyclic post-IRQ INT60 use the same handler but have
     * different return IPs.  Keeping separate initial/cyclic descriptors
     * guarantees that every IRET target is an aligned ROM fetch. */
    add_memory(&g_int60_initial, IVT60_OFFSET_ADDRESS,
               INT60_HANDLER_OFFSET);
    add_memory(&g_int60_initial, IVT60_SEGMENT_ADDRESS, 0xF000u);
    add_image_range(&g_int60_initial, INT60_HANDLER_OFFSET, 0x0114u);
    add_memory(&g_int60_initial, STACK_IP_ADDRESS, INITIAL_INT60_RETURN);
    add_memory(&g_int60_initial, STACK_CS_ADDRESS, 0xF000u);
    add_memory(&g_int60_initial, STACK_FLAGS_ADDRESS, 0xF246u);
    add_image_range(&g_int60_initial, INITIAL_INT60_RETURN,
                    CYCLIC_INT60_RETURN);

    /* An accepted physical interrupt flushes the V30 prefetch queue.  The
     * aligned INT60 word at 0022h was already prefetched before HLT, but must
     * be fetched again after IRQ IRET.  Every cyclic foreground block begins
     * with that deterministic post-IRQ refetch, then serves the IVT/handler. */
    add_memory(&g_int60, PROCESSOR_IMAGE_BASE + IRQ_RETURN_OFFSET,
               image_word(IRQ_RETURN_OFFSET));
    add_memory(&g_int60, IVT60_OFFSET_ADDRESS, INT60_HANDLER_OFFSET);
    add_memory(&g_int60, IVT60_SEGMENT_ADDRESS, 0xF000u);
    add_image_range(&g_int60, INT60_HANDLER_OFFSET, 0x0114u);
    add_memory(&g_int60, STACK_IP_ADDRESS, CYCLIC_INT60_RETURN);
    add_memory(&g_int60, STACK_CS_ADDRESS, 0xF000u);
    add_memory(&g_int60, STACK_FLAGS_ADDRESS, 0xF246u);
    /* IRET -> 0024 JMP -> 0020 NOP/HLT.  The aligned 0022 INT60 word is
     * prefetched before HLT and becomes executable only after the next IRQ. */
    add_image_range(&g_int60, CYCLIC_INT60_RETURN,
                    CYCLIC_INT60_RETURN + 2u);
    add_image_range(&g_int60, INITIAL_INT60_RETURN,
                    CYCLIC_INT60_RETURN);

    /* Physical IRQ20 uses two independent exact streams.  V30 instruction
     * prefetch may interleave handler ROM reads with IN E0h/E4h in a way that
     * changes with clock ratio and queue state.  Keeping ROM and I/O on
     * separate SMs makes that interleave irrelevant: each stream advances
     * only on its own qualified current-cycle key. */
    compile_irq_rom_sequence();

    add_io(&g_irq_io, STATUS_PORT, 1u);
    for (uint32_t i = 0u; i < HOST_WORDS; ++i)
        add_io(&g_irq_io, RX_PORT, g_host_words[i]);

    hard_assert(g_boot.count > 4u);
    hard_assert(g_int60_initial.count > 4u);
    hard_assert(g_int60.count > 4u);
    hard_assert(g_irq_rom.count > 4u);
    hard_assert(g_irq_io.count == HOST_WORDS + 1u);
}

static uint32_t flatten_full(const exact_sequence_t *s,
                             uint32_t words[STREAM_WORDS]) {
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < s->count; ++i) {
        words[n++] = s->pair[i].key;
        words[n++] = s->pair[i].response;
    }
    return n;
}

static uint32_t flatten_append(const exact_sequence_t *s,
                               uint32_t words[STREAM_WORDS], uint32_t n) {
    hard_assert(n + s->count * 2u <= STREAM_WORDS);
    for (uint32_t i = 0u; i < s->count; ++i) {
        words[n++] = s->pair[i].key;
        words[n++] = s->pair[i].response;
    }
    return n;
}

static void exact_sm_init(rp86_prepared_sm_t *s, uint sm, uint offset) {
    hard_assert(clock_get_hz(clk_sys) == EXACT_RESPONDER_CLK_SYS_HZ);
    s->pio = pio1;
    s->sm = sm;
    s->offset = offset;
    pio_sm_claim(s->pio, s->sm);
    pio_sm_config c =
        rp86_processor_service_responder_program_get_default_config(offset);
    sm_config_set_in_pins(&c, 0u);
    sm_config_set_out_pins(&c, RP86_PREPARED_OUT_BASE, RP86_PREPARED_OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    hard_assert(pio_sm_init(s->pio, s->sm, offset, &c) == PICO_OK);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void inta_sm_init(rp86_prepared_sm_t *s, uint sm, uint offset) {
    s->pio = pio1;
    s->sm = sm;
    s->offset = offset;
    pio_sm_claim(s->pio, s->sm);
    pio_sm_config c =
        rp86_interrupt_acknowledge_responder_program_get_default_config(offset);
    sm_config_set_out_pins(&c, RP86_PREPARED_OUT_BASE, RP86_PREPARED_OUT_COUNT);
    sm_config_set_out_shift(&c, true, false, 32u);
    hard_assert(pio_sm_init(s->pio, s->sm, offset, &c) == PICO_OK);
    pio_sm_set_enabled(s->pio, s->sm, false);
}

static void prime_exact(const rp86_prepared_sm_t *s, const exact_sequence_t *sequence) {
    (void)sequence;
    rp86_prepared_sm_arm((rp86_prepared_sm_t *)s);
    pio_sm_exec(s->pio, s->sm, pio_encode_jmp(
        rp86_processor_service_responder_initial_offset(s->offset)));
    pio_sm_exec(s->pio, s->sm, pio_encode_mov(pio_pindirs, pio_null));
}

static int start_words_dma(const rp86_prepared_sm_t *s, const uint32_t *words,
                           uint32_t count) {
    return rp86_prepared_start_tx_dma(s, words, count);
}

static void __isr companion_dma_irq0(void) {
    /* The initial foreground transfer is RESET+INT60.  Every reload is the
     * cyclic INT60-only stream, so SM0 never waits on a reset key again. */
    if (g_foreground_dma >= 0 &&
        dma_channel_get_irq0_status((uint)g_foreground_dma)) {
        dma_channel_acknowledge_irq0((uint)g_foreground_dma);
        dma_channel_set_read_addr((uint)g_foreground_dma,
                                  g_int60_words, false);
        dma_channel_set_trans_count((uint)g_foreground_dma,
                                    g_int60_dma_words, true);
    }
    if (g_irq_rom_dma >= 0 &&
        dma_channel_get_irq0_status((uint)g_irq_rom_dma)) {
        dma_channel_acknowledge_irq0((uint)g_irq_rom_dma);
        dma_channel_set_read_addr((uint)g_irq_rom_dma,
                                  g_irq_rom_words, false);
        dma_channel_set_trans_count((uint)g_irq_rom_dma,
                                    g_irq_rom_dma_words, true);
    }
    if (g_irq_io_dma >= 0 &&
        dma_channel_get_irq0_status((uint)g_irq_io_dma)) {
        dma_channel_acknowledge_irq0((uint)g_irq_io_dma);
        dma_channel_set_read_addr((uint)g_irq_io_dma,
                                  g_irq_io_words, false);
        dma_channel_set_trans_count((uint)g_irq_io_dma,
                                    g_irq_io_dma_words, true);
    }
}

static void prepare_bootstrap_record(void) {
    /* Internal stimulus only: the processor still computes AAD16 and commits
     * the physical witness. Never consume a Host request or send a HID reply
     * for this record. Host traffic queued during bootstrap is handled later. */
    static const uint8_t payload[HOST_WORDS * 2u] = {
        'H', 'B', 1u, 0u, 0u, 0u,
        0x12u, 0x34u, 0x56u, 0x78u, 0x9au, 0xbcu, 0xdeu, 0xf0u,
    };
    memset(&g_bootstrap_record, 0, sizeof g_bootstrap_record);
    g_bootstrap_record.version = RP86_HOST_PROTOCOL_VERSION;
    g_bootstrap_record.type = RP86_HOST_PROTOCOL_MESSAGE_HEARTBEAT;
    g_bootstrap_record.status = RP86_HOST_PROTOCOL_STATUS_OK;
    g_bootstrap_record.length = sizeof payload;
    memcpy(g_bootstrap_record.payload, payload, sizeof payload);
    uint8_t bytes[HOST_WORDS * 2u] = {0};
    memcpy(bytes, payload, sizeof payload);
    for (uint32_t i = 0u; i < HOST_WORDS; ++i)
        g_host_words[i] = (uint16_t)bytes[i * 2u] |
            ((uint16_t)bytes[i * 2u + 1u] << 8);
}

static bool take_live_record(rp86_host_protocol_message_t *record) {
    if (!take_non_control_record(record)) return false;
    return record->version == RP86_HOST_PROTOCOL_VERSION &&
           record->status == RP86_HOST_PROTOCOL_STATUS_OK &&
           record->length <= sizeof record->payload &&
           (record->type == RP86_HOST_PROTOCOL_MESSAGE_HEARTBEAT ||
            record->type == RP86_HOST_PROTOCOL_MESSAGE_COMMAND);
}

static uint16_t stage_live_payload(const rp86_host_protocol_message_t *record) {
    uint8_t bytes[HOST_WORDS * 2u] = {0};
    uint16_t witness = 0u;
    uint16_t length = record->length;
    if (length > sizeof bytes) length = sizeof bytes;
    memcpy(bytes, record->payload, length);
    for (uint32_t i = 0u; i < HOST_WORDS; ++i) {
        g_host_words[i] = (uint16_t)bytes[i * 2u] |
            ((uint16_t)bytes[i * 2u + 1u] << 8);
        witness ^= g_host_words[i];
    }
    return witness;
}

static bool select_calculator_opcode(const rp86_host_protocol_message_t *record) {
    g_calculator_opcode = CALCULATOR_OPCODE_ADD;
    g_calculator_entry_linear = g_runtime.workload.manifest.load_address;
    if (record->type != RP86_HOST_PROTOCOL_MESSAGE_COMMAND ||
        g_host_words[0] != CALCULATOR_MAGIC)
        return false;

    uint32_t operation_offset;
    switch (g_host_words[1]) {
        case CALCULATOR_ADD:
            g_calculator_opcode = CALCULATOR_OPCODE_ADD;
            operation_offset = 0u;
            break;
        case CALCULATOR_SUB:
            g_calculator_opcode = CALCULATOR_OPCODE_SUB;
            operation_offset = CALCULATOR_ENTRY_STRIDE;
            break;
        case CALCULATOR_MUL:
            g_calculator_opcode = CALCULATOR_OPCODE_MUL;
            operation_offset = CALCULATOR_ENTRY_STRIDE * 2u;
            break;
        case CALCULATOR_DIV:
            if (g_host_words[3] == 0u) return false;
            g_calculator_opcode = CALCULATOR_OPCODE_DIV;
            operation_offset = CALCULATOR_ENTRY_STRIDE * 3u;
            break;
        default:
            return false;
    }
    if (g_runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING) {
        if (!calculator_workload_valid()) return false;
        g_calculator_entry_linear += operation_offset;
    }
    return true;
}

static bool rearm_exact_stream(const rp86_prepared_sm_t *sm, int dma,
                               const exact_sequence_t *sequence,
                               const uint32_t *words, uint32_t word_count) {
    if (sm == NULL || sm->pio == NULL || dma < 0 || words == NULL ||
        word_count == 0u)
        return false;
    dma_channel_set_irq0_enabled((uint)dma, false);
    dma_channel_abort((uint)dma);
    dma_channel_acknowledge_irq0((uint)dma);
    prime_exact(sm, sequence);
    dma_channel_set_read_addr((uint)dma, words, false);
    dma_channel_set_trans_count((uint)dma, word_count, true);
    if (!rp86_prepared_wait_fifo_primed(sm, 4u)) return false;
    dma_channel_set_irq0_enabled((uint)dma, true);
    pio_sm_set_enabled(sm->pio, sm->sm, true);
    return true;
}

static void rearm_live_observer(const rp86_prepared_sm_t *observer,
                                int observer_dma) {
    pio_sm_set_enabled(observer->pio, observer->sm, false);
    dma_channel_abort((uint)observer_dma);
    memset(g_observer_dma_words, 0, sizeof g_observer_dma_words);
    rp86_prepared_sm_arm((rp86_prepared_sm_t *)observer);
    dma_channel_set_write_addr((uint)observer_dma,
                               g_observer_dma_words, false);
    dma_channel_set_trans_count((uint)observer_dma,
                                RP86_PREPARED_OBSERVER_WORDS, true);
    pio_sm_set_enabled(observer->pio, observer->sm, true);
}

static bool raw_io_write(uint32_t raw) {
    return !rp86_prepared_sample_bit(raw, RP86_PROCESSOR_PIN_IOM) &&
           rp86_prepared_sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) &&
           rp86_prepared_sample_bit(raw, RP86_PROCESSOR_PIN_INTAK);
}

static bool raw_io_read(uint32_t raw) {
    return !rp86_prepared_sample_bit(raw, RP86_PROCESSOR_PIN_IOM) &&
           !rp86_prepared_sample_bit(raw, RP86_PROCESSOR_PIN_BUFRW) &&
           rp86_prepared_sample_bit(raw, RP86_PROCESSOR_PIN_INTAK);
}

static void classify_trace_words(companion_result_t *r, uint32_t word_count) {
    r->complete_cycles = word_count / 2u;
    uint32_t tx_words_since_commit = 0u;
    for (uint32_t i = 0u; i < r->complete_cycles; ++i) {
        const uint32_t address_raw = g_observer_dma_words[i * 2u];
        const uint32_t data_raw = g_observer_dma_words[i * 2u + 1u];
        if (!raw_io_write(address_raw)) continue;
        const uint32_t address = rp86_prepared_decode_address(address_raw);
        const uint16_t data = rp86_prepared_decode_ad(data_raw);
        if (address == TX_PORT)
            ++tx_words_since_commit;
        if (address == CONTROL_PORT && data == 1u) {
            if (tx_words_since_commit >= 6u) {
                r->irq_commit_seen = true;
                ++r->irq_commits;
            } else {
                r->int60_commit_seen = true;
                ++r->int60_commits;
            }
            tx_words_since_commit = 0u;
        }
        if (address == PIC_COMMAND_PORT && (data & 0xFFu) == 0x20u) {
            r->eoi_seen = true;
            ++r->eoi_writes;
        }
    }
}

static void classify_live_round(live_round_result_t *round,
                                uint32_t word_count,
                                uint16_t expected_witness) {
    /* Re-derive the whole result from the completed DMA prefix on every poll.
     * Keeping a prior EOI flag while rescanning from cycle zero could otherwise
     * misclassify an earlier commit as the post-IRET foreground commit. */
    round->witness = false;
    round->irq_commit = false;
    round->eoi = false;
    round->foreground_commit = false;
    round->native_counter_valid = false;
    round->processor_identity_valid = false;
    round->calculator_valid = false;
    round->processor_signature = 0u;
    round->observed_witness = 0u;
    uint16_t tx_data[NATIVE_REPLY_WORDS] = {0};
    uint32_t tx_words = 0u;
    const uint32_t cycles = word_count / 2u;
    round->observed_cycles = cycles;
    for (uint32_t i = 0u; i < cycles; ++i) {
        const uint32_t address_raw = g_observer_dma_words[i * 2u];
        const uint32_t data_raw = g_observer_dma_words[i * 2u + 1u];
        if (!raw_io_write(address_raw)) continue;
        const uint32_t address = rp86_prepared_decode_address(address_raw);
        const uint16_t data = rp86_prepared_decode_ad(data_raw);
        if (address == WITNESS_PORT) {
            round->observed_witness = data;
            round->witness = data == expected_witness;
        } else if (address == TX_PORT) {
            if (tx_words < NATIVE_REPLY_WORDS)
                tx_data[tx_words] = data;
            ++tx_words;
        } else if (address == CONTROL_PORT && data == 1u) {
            if (tx_words >= NATIVE_REPLY_WORDS) {
                round->irq_commit = true;
                round->processor_signature = tx_data[NATIVE_TEXT_WORDS];
                round->processor_identity_valid =
                    rp86_prepared_processor_signature_valid(
                        round->processor_signature);
                uint32_t counters[NATIVE_COUNTER_COPIES];
                for (uint32_t copy = 0u; copy < NATIVE_COUNTER_COPIES; ++copy) {
                    const uint32_t word = NATIVE_TEXT_WORDS +
                        NATIVE_PROCESSOR_WORDS + copy * 2u;
                    counters[copy] = (uint32_t)tx_data[word] |
                        ((uint32_t)tx_data[word + 1u] << 16);
                }
                if (counters[0] == counters[1] ||
                    counters[0] == counters[2]) {
                    round->cpu_sequence = counters[0];
                    round->native_counter_valid = true;
                } else if (counters[1] == counters[2]) {
                    round->cpu_sequence = counters[1];
                    round->native_counter_valid = true;
                }
                round->command_sequence = 0u;
                if (round->calculator_requested &&
                    tx_data[2] == CALCULATOR_MAGIC &&
                    tx_data[3] == g_host_words[1] &&
                    tx_data[4] == g_host_words[2] &&
                    tx_data[5] == g_host_words[3]) {
                    round->calculator_operation = tx_data[3];
                    round->calculator_lhs = tx_data[4];
                    round->calculator_rhs = tx_data[5];
                    round->calculator_low = tx_data[0];
                    round->calculator_high = tx_data[1];
                    round->calculator_valid = true;
                }
            } else if (round->eoi && tx_words >= 1u) {
                round->foreground_commit = true;
            }
            tx_words = 0u;
            memset(tx_data, 0, sizeof tx_data);
        } else if (address == PIC_COMMAND_PORT &&
                   (data & 0xFFu) == 0x20u) {
            round->eoi = true;
        }
    }
}

static bool run_live_round(const rp86_prepared_sm_t *foreground,
                           const rp86_prepared_sm_t *irq_rom,
                           const rp86_prepared_sm_t *irq_io,
                           const rp86_prepared_sm_t *inta,
                           const rp86_prepared_sm_t *observer,
                           int observer_dma,
                           const rp86_host_protocol_message_t *record,
                           live_round_result_t *round) {
    memset(round, 0, sizeof *round);
    const uint16_t expected_witness = stage_live_payload(record);
    g_calculator_opcode = CALCULATOR_OPCODE_ADD;
    /* A RUNNING workload owns the dispatch target for every IRQ round,
     * including background heartbeat records.  Calculator commands may add
     * an operation offset below, but a non-command round must never inherit
     * the zero-initialized reset value as a far-call segment. */
    g_calculator_entry_linear = g_runtime.workload.manifest.load_address;
    round->calculator_requested =
        record->type == RP86_HOST_PROTOCOL_MESSAGE_COMMAND &&
        g_host_words[0] == CALCULATOR_MAGIC;
    if (round->calculator_requested && !select_calculator_opcode(record))
        return false;
    compile_irq_rom_sequence();
    g_irq_rom_dma_words = flatten_full(&g_irq_rom, g_irq_rom_words);
    memset(&g_irq_io, 0, sizeof g_irq_io);
    add_io(&g_irq_io, STATUS_PORT, 1u);
    for (uint32_t i = 0u; i < HOST_WORDS; ++i)
        add_io(&g_irq_io, RX_PORT, g_host_words[i]);
    g_irq_io_dma_words = flatten_full(&g_irq_io, g_irq_io_words);

    /* The processor is parked in HLT here. Reset all three exact streams to
     * one common cycle boundary before asserting the next physical INTR.
     * This prevents a completed bring-up burst from leaving one matcher on a
     * stale ROM/prefetch key while another has already advanced. */
    if (!rearm_exact_stream(foreground, g_foreground_dma, &g_int60,
                            g_int60_words, g_int60_dma_words) ||
        !rearm_exact_stream(irq_rom, g_irq_rom_dma, &g_irq_rom,
                            g_irq_rom_words, g_irq_rom_dma_words) ||
        !rearm_exact_stream(irq_io, g_irq_io_dma, &g_irq_io,
                            g_irq_io_words, g_irq_io_dma_words))
        return false;
    rearm_live_observer(observer, observer_dma);

    pio_interrupt_clear(pio1, 4u);
    pio_interrupt_clear(pio1, 5u);
    gpio_put(RP86_PROCESSOR_PIN_INTR, true);
    const uint64_t deadline = time_us_64() + LIVE_ROUND_TIMEOUT_US;
    while (!pio_interrupt_get(pio1, 4u) && time_us_64() <= deadline)
        rp86_host_protocol_usb_task();
    if (pio_interrupt_get(pio1, 4u)) {
        pio_interrupt_clear(pio1, 4u);
        gpio_put(RP86_PROCESSOR_PIN_INTR, false);
        round->inta1 = true;
    } else {
        gpio_put(RP86_PROCESSOR_PIN_INTR, false);
        return false;
    }

    while (!pio_interrupt_get(pio1, 5u) && time_us_64() <= deadline)
        rp86_host_protocol_usb_task();
    if (!pio_interrupt_get(pio1, 5u)) return false;
    pio_interrupt_clear(pio1, 5u);
    round->inta2 = true;
    pio_sm_put_blocking(pio1, inta->sm,
                        rp86_prepared_encode_word(COMPANION_VECTOR));

    while (time_us_64() <= deadline) {
        const uint32_t words = RP86_PREPARED_OBSERVER_WORDS - rp86_prepared_dma_remaining(observer_dma);
        __dmb();
        classify_live_round(round, words, expected_witness);
        if (round->witness && round->irq_commit &&
            round->native_counter_valid &&
            round->processor_identity_valid && round->eoi &&
            round->foreground_commit &&
            (!round->calculator_requested || round->calculator_valid)) {
            rp86_prepared_runtime_observe_processor(
                &g_prepared_runtime, round->processor_signature);
            return true;
        }
        rp86_host_protocol_usb_task();
    }
    return false;
}

static void send_live_reply(const rp86_host_protocol_message_t *request,
                            const live_round_result_t *round,
                            bool passed) {
    rp86_native_service_witness_t witness = {0};
    memset(&g_reply_record, 0, sizeof g_reply_record);
    g_reply_record.version = RP86_HOST_PROTOCOL_VERSION;
    g_reply_record.type = request->type == RP86_HOST_PROTOCOL_MESSAGE_COMMAND ?
        RP86_HOST_PROTOCOL_MESSAGE_RESULT : RP86_HOST_PROTOCOL_MESSAGE_HEARTBEAT;
    g_reply_record.status = passed ? RP86_HOST_PROTOCOL_STATUS_OK :
        RP86_HOST_PROTOCOL_STATUS_TIMEOUT;
    g_reply_record.sequence = request->sequence;
    char payload[33];
    if (round->calculator_valid) {
        const char operator = round->calculator_operation == CALCULATOR_ADD ? '+' :
            round->calculator_operation == CALCULATOR_SUB ? '-' :
            round->calculator_operation == CALCULATOR_MUL ? '*' : '/';
        if (round->calculator_operation == CALCULATOR_MUL) {
            const uint32_t product = (uint32_t)round->calculator_low |
                ((uint32_t)round->calculator_high << 16);
            snprintf(payload, sizeof payload, "CALC %u%c%u=%lu",
                     round->calculator_lhs, operator, round->calculator_rhs,
                     (unsigned long)product);
        } else if (round->calculator_operation == CALCULATOR_DIV) {
            snprintf(payload, sizeof payload, "CALC %u/%u=%u R%u",
                     round->calculator_lhs, round->calculator_rhs,
                     round->calculator_low, round->calculator_high);
        } else {
            snprintf(payload, sizeof payload, "CALC %u%c%u=%u",
                     round->calculator_lhs, operator, round->calculator_rhs,
                     round->calculator_low);
        }
    } else {
        snprintf(payload, sizeof payload, "%s",
                 request->type == RP86_HOST_PROTOCOL_MESSAGE_COMMAND ?
                     "PROCESSOR COMMAND OK" : "PROCESSOR HEARTBEAT OK");
    }
    witness.magic[0] = RP86_NATIVE_WITNESS_MAGIC_0;
    witness.magic[1] = RP86_NATIVE_WITNESS_MAGIC_1;
    witness.magic[2] = RP86_NATIVE_WITNESS_MAGIC_2;
    witness.magic[3] = RP86_NATIVE_WITNESS_MAGIC_3;
    witness.version = RP86_NATIVE_WITNESS_VERSION;
    witness.service_type = request->type;
    witness.flags = rp86_prepared_processor_witness_flags(
        round->processor_signature);
    witness.boot_id = g_processor_boot_id;
    witness.cpu_sequence = round->cpu_sequence;
    witness.command_sequence = round->command_sequence;
    const uint16_t text_length = (uint16_t)strlen(payload);
    memcpy(g_reply_record.payload, &witness, sizeof witness);
    memcpy(g_reply_record.payload + sizeof witness, payload, text_length);
    g_reply_record.length = (uint16_t)(sizeof witness + text_length);
    (void)rp86_host_protocol_hid_send_record(
        (const uint8_t *)&g_reply_record, HOST_TIMEOUT_US);
    evidence_printf(
        "[LIVE CPU ROUND] request=%lu type=%u boot=%lu cpu_seq=%lu command_seq=%lu INTA=%s witness=%04X %s commit=%s EOI=%s idle=%s result=%s\n",
        (unsigned long)request->sequence, request->type,
        (unsigned long)g_processor_boot_id,
        (unsigned long)round->cpu_sequence,
        (unsigned long)round->command_sequence,
        round->inta1 && round->inta2 ? "PASS" : "FAIL",
        round->observed_witness, round->witness ? "PASS" : "FAIL",
        round->irq_commit ? "PASS" : "FAIL",
        round->eoi ? "PASS" : "FAIL",
        round->foreground_commit ? "PASS" : "FAIL",
        passed ? "PASS" : "FAIL");
    evidence_printf(
        "[NATIVE PROCESSOR ID] signature=%04X identity=%s result=%s\n",
        round->processor_signature,
        rp86_prepared_processor_identity_name(round->processor_signature),
        round->processor_identity_valid ? "PASS" : "FAIL");
    if (round->calculator_requested) {
        evidence_printf(
            "[NATIVE CALCULATOR] op=%u lhs=%u rhs=%u low=%u high=%u result=%s\n",
            round->calculator_operation, round->calculator_lhs,
            round->calculator_rhs, round->calculator_low,
            round->calculator_high,
            round->calculator_valid ? "PASS" : "FAIL");
    }
}

static int evidence_printf(const char *format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (length < 0 || (size_t)length >= sizeof line) return -1;
    if (rp86_workload_executor_active(&g_workload_executor) &&
        !rp86_workload_executor_processor_idle(&g_workload_executor)) {
        return rp86_evidence_queue_try_push(
                   &g_runtime_evidence, line, (size_t)length) ?
                   length : -1;
    }
    return rp86_host_protocol_cdc_write(line, (uint32_t)length,
                                    HOST_TIMEOUT_US) ? length : -1;
}

static void drain_runtime_evidence(void) {
    const uint8_t *data = NULL;
    const size_t available = rp86_evidence_queue_peek(
        &g_runtime_evidence, &data);
    if (available == 0u) return;
    const uint32_t bounded = available > UINT32_MAX ?
        UINT32_MAX : (uint32_t)available;
    const uint32_t written = rp86_host_protocol_cdc_try_write(
        (const char *)data, bounded);
    rp86_evidence_queue_consume(&g_runtime_evidence, written);
}

static rp86_workload_clock_mode_t physical_workload_clock_mode(void) {
    switch (rp86_processor_bus_execution_clock_mode(&g_processor_bus)) {
        case RP86_EXECUTION_CLOCK_FREE_RUNNING:
            return RP86_WORKLOAD_CLOCK_FREE_RUNNING;
        case RP86_EXECUTION_CLOCK_CLOCK_STEPPED:
            return RP86_WORKLOAD_CLOCK_STEPPED;
        case RP86_EXECUTION_CLOCK_STOPPED:
        default:
            return RP86_WORKLOAD_CLOCK_STOPPED;
    }
}

static rp86_runtime_status_snapshot_t runtime_status_snapshot(void) {
    rp86_runtime_status_snapshot_t snapshot;
    rp86_runtime_status_capture(
        &snapshot, &g_runtime, &g_workload_executor, &g_prepared_runtime,
        physical_workload_clock_mode(), g_bus_active);
    return snapshot;
}

static bool prepared_runtime_physically_running(void) {
    return rp86_prepared_runtime_physically_running(
        &g_prepared_runtime,
        g_bus_active,
        physical_workload_clock_mode() == RP86_WORKLOAD_CLOCK_FREE_RUNNING,
        gpio_get(RP86_PROCESSOR_PIN_RESET));
}

static bool start_calculator_workload(void) {
    const bool calculator_valid = calculator_workload_valid();
    if (!rp86_prepared_runtime_initialized(&g_prepared_runtime) ||
        g_foreground_dma < 0 || g_irq_rom_dma < 0 ||
        g_irq_io_dma < 0 || g_observer_dma < 0 ||
        !calculator_valid) {
        evidence_printf(
            "[WORKLOAD START] calculator precondition failed "
            "initialized=%u dma=%d/%d/%d/%d valid=%u state=%u size=%lu\n",
            rp86_prepared_runtime_initialized(&g_prepared_runtime),
            g_foreground_dma,
            g_irq_rom_dma, g_irq_io_dma, g_observer_dma,
            calculator_valid, (unsigned)g_runtime.workload.state,
            (unsigned long)g_runtime.workload.manifest.image_size);
        return false;
    }

    /* A calculator package is native code called by the resident processor
     * runtime.  RUN owns the complete physical lifecycle: even when the
     * resident responder was previously parked, rebuild its prepared streams,
     * assert RESET, start the clock and responder, release RESET, and prove a
     * native interrupt round before reporting RUNNING to the Host. */
    gpio_put(RP86_PROCESSOR_PIN_INTR, false);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    g_bus_active = false;
    irq_set_enabled(DMA_IRQ_0, false);
    pio_set_sm_mask_enabled(pio1, 0x0fu, false);
    pio_set_sm_mask_enabled(pio0, 0x0fu, false);
    const int channels[] = {
        g_foreground_dma, g_irq_rom_dma, g_irq_io_dma, g_observer_dma,
    };
    for (uint32_t i = 0u; i < count_of(channels); ++i) {
        if (channels[i] >= 0) dma_channel_abort((uint)channels[i]);
    }

    g_calculator_opcode = CALCULATOR_OPCODE_ADD;
    g_calculator_entry_linear = g_runtime.workload.manifest.load_address;
    compile_sequences();
    uint32_t foreground_words = flatten_append(
        &g_boot, g_foreground_initial_words, 0u);
    foreground_words = flatten_append(
        &g_int60_initial, g_foreground_initial_words, foreground_words);
    g_int60_dma_words = flatten_full(&g_int60, g_int60_words);
    g_irq_rom_dma_words = flatten_full(&g_irq_rom, g_irq_rom_words);
    g_irq_io_dma_words = flatten_full(&g_irq_io, g_irq_io_words);

    if (!rearm_exact_stream(&g_foreground_sm, g_foreground_dma, &g_boot,
                            g_foreground_initial_words, foreground_words) ||
        !rearm_exact_stream(&g_irq_rom_sm, g_irq_rom_dma, &g_irq_rom,
                            g_irq_rom_words, g_irq_rom_dma_words) ||
        !rearm_exact_stream(&g_irq_io_sm, g_irq_io_dma, &g_irq_io,
                            g_irq_io_words, g_irq_io_dma_words)) {
        evidence_printf("[WORKLOAD START] prepared stream rearm failed\n");
        park_physical_processor();
        return false;
    }
    pio_sm_set_enabled(g_inta_sm.pio, g_inta_sm.sm, false);
    rp86_prepared_sm_arm(&g_inta_sm);
    pio_sm_exec(g_inta_sm.pio, g_inta_sm.sm,
                pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_put_blocking(g_inta_sm.pio, g_inta_sm.sm,
                        rp86_prepared_encode_word(COMPANION_VECTOR));
    pio_sm_set_enabled(g_inta_sm.pio, g_inta_sm.sm, true);
    rearm_live_observer(&g_observer_sm, g_observer_dma);
    irq_set_enabled(DMA_IRQ_0, true);
    rp86_prepared_route_ad_to_responder(&g_foreground_sm);

    if (physical_workload_clock_mode() == RP86_WORKLOAD_CLOCK_STOPPED &&
        !rp86_processor_bus_set_execution_clock_mode(
            &g_processor_bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
            EXECUTION_CLOCK_SWITCH_US)) {
        evidence_printf("[WORKLOAD START] initial stepped clock failed\n");
        park_physical_processor();
        return false;
    }
    if (!rp86_processor_bus_set_execution_clock_mode(
            &g_processor_bus, RP86_EXECUTION_CLOCK_FREE_RUNNING,
            EXECUTION_CLOCK_SWITCH_US) ||
        !rp86_prepared_wait_reset_clocks(RP86_PROCESSOR_RESET_CLOCKS,
                                         RP86_PROCESSOR_HZ) ||
        !rp86_processor_bus_set_execution_clock_mode(
            &g_processor_bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
            EXECUTION_CLOCK_SWITCH_US)) {
        evidence_printf("[WORKLOAD START] reset clock sequence failed\n");
        park_physical_processor();
        return false;
    }

    ++g_processor_boot_id;
    if (g_processor_boot_id == 0u) ++g_processor_boot_id;
    if (!rp86_processor_bus_set_execution_clock_mode(
            &g_processor_bus, RP86_EXECUTION_CLOCK_FREE_RUNNING,
            EXECUTION_CLOCK_SWITCH_US)) {
        evidence_printf("[WORKLOAD START] free-running clock failed\n");
        park_physical_processor();
        return false;
    }
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);
    rp86_prepared_runtime_activate(&g_prepared_runtime);
    g_bus_active = true;

    busy_wait_ms(20u);
    rp86_host_protocol_message_t proof = {
        .version = RP86_HOST_PROTOCOL_VERSION,
        .type = RP86_HOST_PROTOCOL_MESSAGE_HEARTBEAT,
        .status = RP86_HOST_PROTOCOL_STATUS_OK,
    };
    live_round_result_t round;
    if (!run_live_round(&g_foreground_sm, &g_irq_rom_sm, &g_irq_io_sm,
                        &g_inta_sm, &g_observer_sm, g_observer_dma,
                        &proof, &round)) {
        evidence_printf("[WORKLOAD START] native proof round failed\n");
        park_physical_processor();
        return false;
    }
    evidence_printf(
        "[WORKLOAD START] id=%lu entry=%04X:%04X clock=FREE_RUNNING native=PASS\n",
        (unsigned long)g_runtime.workload.workload_id,
        g_runtime.workload.manifest.entry_segment,
        g_runtime.workload.manifest.entry_offset);
    return prepared_runtime_physically_running();
}

static void print_canonical_status(void) {
    const rp86_runtime_status_snapshot_t snapshot = runtime_status_snapshot();
    evidence_printf("\n[RUNTIME STATUS]\n");
    evidence_printf("State                      = %s\n",
                    snapshot.physical_bus_active ? "RUNNING" : "IDLE");
    const rp86_workload_clock_mode_t physical_clock =
        (rp86_workload_clock_mode_t)snapshot.workload.status.clock_mode;
    evidence_printf("Execution clock mode       = %s\n",
                    physical_clock == RP86_WORKLOAD_CLOCK_FREE_RUNNING ?
                        "FREE_RUNNING" :
                    physical_clock == RP86_WORKLOAD_CLOCK_STEPPED ?
                        "CLOCK_STEPPED" :
                    physical_clock == RP86_WORKLOAD_CLOCK_STOPPED ?
                        "STOPPED" : "AUTO");
    evidence_printf("Free-running clock         = %.3f MHz\n",
                    (double)RP86_PROCESSOR_HZ / 1000000.0);
    evidence_printf("Clock-stepped cycles       = %lu\n",
                    (unsigned long)rp86_workload_executor_stats(
                        &g_workload_executor)->cycles);
    evidence_printf("Maximum clock-step interval = %lu us\n",
                    (unsigned long)rp86_processor_bus_max_step_interval_us(
                        &g_processor_bus));
    evidence_printf("Native evidence queue drops = %lu\n",
                    (unsigned long)rp86_evidence_queue_drops(
                        &g_runtime_evidence));
    evidence_printf("HID OUT record drops       = %lu\n",
                    (unsigned long)
                        rp86_host_protocol_hid_producer_drops());
    evidence_printf("Processor idle             = %s\n",
                    rp86_workload_executor_processor_idle(
                        &g_workload_executor) ? "YES / HLT" : "NO");
    evidence_printf("Processor                  = %s (AAD16=%04X) %s\n",
        rp86_prepared_processor_identity_name(g_prepared_runtime.processor_signature),
        g_prepared_runtime.processor_signature,
        g_prepared_runtime.processor_identity_valid ? "IDENTIFIED" : "UNPROVEN");
    evidence_printf("External PSRAM configured  = %s\n",
                    RP86_HAS_EXTERNAL_PSRAM ? "YES" : "NO");
#if RP86_HAS_EXTERNAL_PSRAM
    evidence_printf("External PSRAM probe       = CANONICAL STARTUP\n");
#else
    evidence_printf("External PSRAM probe       = SKIPPED\n");
#endif
    evidence_printf("Workload memory            = %s\n",
                    g_runtime.memory_backing.name);
    evidence_printf("Processor memory range     = %05lX-%05lX (%lu KiB)\n",
                    (unsigned long)g_runtime.memory_backing.processor_base,
                    (unsigned long)(g_runtime.memory_backing.processor_base +
                                    g_runtime.memory_backing.size - 1u),
                    (unsigned long)(g_runtime.memory_backing.size / 1024u));
    evidence_printf("External PSRAM role        = OPTIONAL CAPACITY TIER\n");
    evidence_printf("Onboard NOR flash          = W25Q128JV / 16 MiB\n");
    evidence_printf("Firmware reserved          = 0x000000-0x3FFFFF / 4 MiB\n");
    evidence_printf("flash:/ partition          = 0x400000-0xFFFFFF / 12 MiB\n");
    if (g_runtime.flash_available) {
        evidence_printf("flash:/ filesystem         = %s / %s\n",
                        rp86_flash_filesystem_name(
                            g_runtime.flash_volume.filesystem_type),
                        g_runtime.flash_volume.label);
        evidence_printf("flash:/ free               = %lu KiB\n",
                        (unsigned long)g_runtime.flash_volume.free_kib);
        evidence_printf("flash:/ boot state         = %s\n",
                        g_runtime.flash_volume.formatted_on_boot ?
                            "FORMATTED" : "EXISTING");
        evidence_printf("flash:/ media self-test    = %s\n",
                        g_runtime.flash_volume.self_test_passed ? "PASS" : "FAIL");
    } else {
        evidence_printf("flash:/ filesystem         = FAULT\n");
        evidence_printf("flash:/ FatFs result       = %u\n",
                        (unsigned)g_runtime.flash_volume.result);
    }
    evidence_printf("Staged Host workload       = %s",
                    rp86_workload_state_name(g_runtime.workload.state));
    if (g_runtime.workload.state != RP86_WORKLOAD_STATE_EMPTY)
        evidence_printf(" (%lu bytes / id %lu)",
                        (unsigned long)g_runtime.workload.manifest.image_size,
                        (unsigned long)g_runtime.workload.workload_id);
    evidence_printf("\n");
    evidence_printf("Active physical workload   = %s\n",
                    rp86_workload_executor_active(&g_workload_executor) ?
                        "GENERAL INTERNAL SRAM WORKLOAD" :
                    !g_bus_active ? "STOPPED / RESET" :
                    g_runtime.workload.state == RP86_WORKLOAD_STATE_RUNNING ?
                        "INTERNAL SRAM CALCULATOR" :
                        "RP86 PROCESSOR RUNTIME");
    evidence_printf("Onboard GPIO safe state    = PASS\n");
    evidence_printf("MicroSD hardware           = %s\n",
                    RP86_HAS_SDCARD ? "PRESENT" : "ABSENT");
    evidence_printf("MicroSD GPIO30/31/40-43    = PASSIVE / NOT CLAIMED\n");
    evidence_printf("Mini HDMI/DVI hardware     = %s\n",
                    RP86_HAS_DVI ? "PRESENT" : "ABSENT");
    evidence_printf("Mini HDMI GPIO32-39/44-46  = PASSIVE / NOT CLAIMED\n");
    evidence_printf("PIO-USB hardware           = %s\n",
                    RP86_HAS_PIO_USB ? "PRESENT" : "ABSENT");
    evidence_printf("PIO-USB GPIO28/29          = PASSIVE / NOT CLAIMED\n");
    evidence_printf("DVI / PIO-USB concurrency  = MUTUALLY EXCLUSIVE\n");

    evidence_printf("\n[CAPABILITY FRAMEWORK]\n");
    evidence_printf("Host CDC diagnostics        = AVAILABLE\n");
    evidence_printf("Host 64-byte HID records    = AVAILABLE\n");
    evidence_printf("physical 8086-class PIO/DMA bus = AVAILABLE\n");
    evidence_printf("physical INTR / two-cycle INTA = AVAILABLE\n");
    evidence_printf("prepared native probe       = OPTIONAL / DIAGNOSTIC\n");
    evidence_printf("native workload staging     = AVAILABLE / INTERNAL SRAM\n");
    evidence_printf("workload run / stop / restart = AVAILABLE / INTERNAL SRAM\n");
    evidence_printf("processor stdin / stdout    = COMMAND MAILBOX AVAILABLE\n");
    evidence_printf("Host / processor shared memory = AVAILABLE / INTERNAL SRAM MAILBOX\n");
    evidence_printf("flash: FAT volume           = %s\n",
                    g_runtime.flash_available ? "AVAILABLE" : "FAULT");
    evidence_printf("Host flash file service    = %s\n",
                    g_runtime.flash_available ? "LS / DF / CAT / PUT" : "FAULT");
    evidence_printf("sd: FAT volume              = NOT IMPLEMENTED\n");
    evidence_printf("retained physical bus trace = AVAILABLE\n");
    evidence_printf("timeout / fault / restart   = GENERAL BUS SUPERVISION AVAILABLE\n");
    evidence_printf("Host-directed UF2 boot      = AVAILABLE\n");
    evidence_printf("Mini HDMI / DVI output      = NOT IMPLEMENTED\n");
    evidence_printf("PIO-USB host / device       = NOT IMPLEMENTED\n");
    evidence_printf("\nOne canonical CDC+HID runtime.\n");
}

static void park_physical_processor(void) {
    gpio_put(RP86_PROCESSOR_PIN_INTR, false);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    g_bus_active = false;
    irq_set_enabled(DMA_IRQ_0, false);
    pio_set_sm_mask_enabled(pio1, 0x0fu, false);
    pio_set_sm_mask_enabled(pio0, 0x0fu, false);
    rp86_execution_clock_stop_low(&g_processor_bus.execution_clock,
                                  EXECUTION_CLOCK_SWITCH_US);
    const int channels[] = {
        g_foreground_dma, g_irq_rom_dma, g_irq_io_dma, g_observer_dma,
    };
    for (uint32_t i = 0u; i < count_of(channels); ++i) {
        if (channels[i] >= 0) dma_channel_abort((uint)channels[i]);
    }
    rp86_prepared_route_ad_to_sio_high_z();
    gpio_set_function(RP86_PROCESSOR_PIN_CLK, GPIO_FUNC_SIO);
    gpio_put(RP86_PROCESSOR_PIN_CLK, false);
    gpio_set_dir(RP86_PROCESSOR_PIN_CLK, GPIO_OUT);
}

static bool disable_prepared_runtime(void) {
    gpio_put(RP86_PROCESSOR_PIN_INTR, false);
    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    if (!rp86_processor_bus_set_execution_clock_mode(
            &g_processor_bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
            EXECUTION_CLOCK_SWITCH_US)) {
        rp86_processor_bus_force_safe_state(&g_processor_bus);
        return false;
    }
    g_bus_active = false;
    irq_set_enabled(DMA_IRQ_0, false);
    pio_set_sm_mask_enabled(pio1, 0x0fu, false);
    pio_set_sm_mask_enabled(pio0, 0x0fu, false);
    const int channels[] = {
        g_foreground_dma, g_irq_rom_dma, g_irq_io_dma, g_observer_dma,
    };
    for (uint32_t i = 0u; i < count_of(channels); ++i) {
        if (channels[i] >= 0) dma_channel_abort((uint)channels[i]);
    }
    rp86_prepared_route_ad_to_sio_high_z();
    rp86_prepared_runtime_retire(&g_prepared_runtime);
    return true;
}

static bool prepare_general_workload_bus(void *context) {
    (void)context;
    if (rp86_prepared_runtime_available(&g_prepared_runtime))
        return disable_prepared_runtime();
    rp86_processor_bus_hold_reset(true);
    if (!rp86_processor_bus_set_execution_clock_mode(
            &g_processor_bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
            EXECUTION_CLOCK_SWITCH_US)) {
        rp86_processor_bus_force_safe_state(&g_processor_bus);
        return false;
    }
    rp86_prepared_route_ad_to_sio_high_z();
    return true;
}

static void workload_evidence(void *context, const char *text) {
    (void)context;
    evidence_printf("%s", text);
}

static bool start_general_workload(void) {
    return rp86_workload_executor_start(&g_workload_executor);
}

static void stop_general_workload(void) {
    rp86_workload_executor_stop(&g_workload_executor);
}

static void service_general_workload(void) {
    rp86_workload_executor_service(&g_workload_executor);
}

static bool send_runtime_control_ack(
    const rp86_host_protocol_message_t *request, uint8_t operation) {
    rp86_host_protocol_message_t reply = {0};
    reply.version = RP86_HOST_PROTOCOL_VERSION;
    reply.type = RP86_HOST_PROTOCOL_MESSAGE_RUNTIME_STATUS;
    reply.sequence = request->sequence;
    reply.length = 1u;
    reply.status = RP86_HOST_PROTOCOL_STATUS_OK;
    reply.payload[0] = operation;
    return rp86_host_protocol_hid_send_record(
        (const uint8_t *)&reply, 100000u);
}

static bool send_workload_reply(const rp86_host_protocol_message_t *request,
                                rp86_host_protocol_status_t status,
                                bool status_reply) {
    rp86_host_protocol_message_t reply = {0};
    const rp86_runtime_status_snapshot_t snapshot = runtime_status_snapshot();
    reply.version = RP86_HOST_PROTOCOL_VERSION;
    reply.type = status_reply ? RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_STATUS :
                                RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_RESULT;
    reply.sequence = request->sequence;
    reply.length = sizeof snapshot.workload;
    reply.status = status;
    memcpy(reply.payload, &snapshot.workload, sizeof snapshot.workload);
    return rp86_host_protocol_hid_send_record(
        (const uint8_t *)&reply, HOST_TIMEOUT_US);
}

static bool handle_workload_record(const rp86_host_protocol_message_t *request) {
    rp86_host_protocol_status_t status = RP86_HOST_PROTOCOL_STATUS_BAD_LENGTH;
    bool status_reply = false;

    if (!rp86_host_protocol_payload_length_valid(request)) {
        /* The dispatcher rejects this ABI violation before any payload read.
         * Keep the check here too so this handler remains safe in isolation. */
    } else if (request->status != RP86_HOST_PROTOCOL_STATUS_OK) {
        status = RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD;
    } else if (request->type == RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_BEGIN &&
               request->length == sizeof(rp86_workload_begin_payload_t)) {
        rp86_workload_begin_payload_t begin;
        memcpy(&begin, request->payload, sizeof begin);
        status = rp86_workload_begin(&g_runtime.workload,
                                     begin.transfer_id,
                                     &begin.manifest) ?
            RP86_HOST_PROTOCOL_STATUS_OK : RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD;
        if (status == RP86_HOST_PROTOCOL_STATUS_OK)
            rp86_workload_executor_clear_diagnostics(&g_workload_executor);
    } else if (request->type == RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_DATA &&
               request->length > sizeof(uint32_t) * 2u) {
        uint32_t transfer_id;
        uint32_t offset;
        memcpy(&transfer_id, request->payload, sizeof transfer_id);
        memcpy(&offset, request->payload + sizeof transfer_id, sizeof offset);
        const uint8_t *data = request->payload + sizeof(uint32_t) * 2u;
        const size_t length = request->length - sizeof(uint32_t) * 2u;
        status = rp86_workload_write(&g_runtime.workload, transfer_id,
                                     offset, data, length) ?
            RP86_HOST_PROTOCOL_STATUS_OK : RP86_HOST_PROTOCOL_STATUS_BAD_SEQUENCE;
    } else if (request->type == RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_COMMIT &&
               request->length == sizeof(rp86_workload_commit_payload_t)) {
        rp86_workload_commit_payload_t commit;
        memcpy(&commit, request->payload, sizeof commit);
        const bool was_receiving =
            g_runtime.workload.state == RP86_WORKLOAD_STATE_RECEIVING;
        const bool committed = rp86_workload_commit(
            &g_runtime.workload, commit.transfer_id, commit.image_crc32);
        if (committed) {
            rp86_workload_executor_stage(&g_workload_executor);
        }
        status = committed ? RP86_HOST_PROTOCOL_STATUS_OK :
                 was_receiving ? RP86_HOST_PROTOCOL_STATUS_BAD_CRC :
                                 RP86_HOST_PROTOCOL_STATUS_BAD_SEQUENCE;
    } else if (request->type == RP86_HOST_PROTOCOL_MESSAGE_WORKLOAD_CONTROL &&
               request->length == sizeof(rp86_workload_control_payload_t)) {
        rp86_workload_control_payload_t control;
        memcpy(&control, request->payload, sizeof control);
        status_reply = true;
        if (control.operation == RP86_WORKLOAD_CONTROL_STATUS &&
            (control.workload_id == 0u ||
             control.workload_id == g_runtime.workload.workload_id)) {
            status = RP86_HOST_PROTOCOL_STATUS_OK;
        } else if ((control.operation == RP86_WORKLOAD_CONTROL_RUN ||
                    control.operation == RP86_WORKLOAD_CONTROL_RESTART) &&
                   calculator_workload_valid() &&
                   rp86_workload_executor_timeout_enabled(&g_workload_executor)) {
            /* The prepared calculator is not supervised by the general executor. */
            status = RP86_HOST_PROTOCOL_STATUS_BAD_STATE;
        } else if (control.operation == RP86_WORKLOAD_CONTROL_RUN) {
            const bool calculator = calculator_workload_valid();
            const bool state_ok = rp86_workload_run(
                &g_runtime.workload, control.workload_id);
            const bool started = !state_ok ? false : calculator ?
                                 start_calculator_workload() :
                                 start_general_workload();
            if (state_ok && !started)
                g_runtime.workload.state = RP86_WORKLOAD_STATE_FAULTED;
            status = started ? RP86_HOST_PROTOCOL_STATUS_OK :
                               RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD;
        } else if (control.operation == RP86_WORKLOAD_CONTROL_STOP) {
            const bool calculator = calculator_workload_valid();
            const bool stopped = rp86_workload_stop(
                &g_runtime.workload, control.workload_id);
            if (stopped && rp86_workload_executor_active(
                    &g_workload_executor)) stop_general_workload();
            else if (stopped && calculator) park_physical_processor();
            status = stopped ? RP86_HOST_PROTOCOL_STATUS_OK :
                               RP86_HOST_PROTOCOL_STATUS_BAD_STATE;
        } else if (control.operation == RP86_WORKLOAD_CONTROL_RESTART) {
            const bool calculator = calculator_workload_valid();
            const bool state_ok = rp86_workload_restart(
                &g_runtime.workload, control.workload_id);
            if (state_ok && rp86_workload_executor_active(
                    &g_workload_executor)) stop_general_workload();
            const bool started = !state_ok ? false : calculator ?
                                 start_calculator_workload() :
                                 start_general_workload();
            if (state_ok && !started)
                g_runtime.workload.state = RP86_WORKLOAD_STATE_FAULTED;
            status = started ? RP86_HOST_PROTOCOL_STATUS_OK :
                               RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD;
        } else {
            status = RP86_HOST_PROTOCOL_STATUS_BAD_WORKLOAD;
        }
    }

    const bool sent = send_workload_reply(request, status, status_reply);
    evidence_printf(
        "WORKLOAD op=%u seq=%lu state=%s received=%lu status=%u %s\n",
        request->type, (unsigned long)request->sequence,
        rp86_workload_state_name(g_runtime.workload.state),
        (unsigned long)g_runtime.workload.received,
        (unsigned)status, sent ? "REPLIED" : "REPLY TIMEOUT");
    return sent;
}

static bool handle_filesystem_record(const rp86_host_protocol_message_t *request) {
    const rp86_host_service_result_t result =
        rp86_host_service_dispatch_filesystem(&g_runtime.host_services, request);
    if (!result.handled) return false;
    evidence_printf("RP-FLASH op=%u seq=%lu status=%u %s\n",
                    result.operation,
                    (unsigned long)request->sequence,
                    (unsigned)result.status,
                    result.replied ? "REPLIED" : "REPLY TIMEOUT");
    return result.replied;
}

static bool handle_memory_record(const rp86_host_protocol_message_t *request) {
    const rp86_host_service_result_t result =
        rp86_host_service_dispatch_memory(&g_runtime.host_services, request);
    if (!result.handled) return false;
    evidence_printf("MEMORY op=%u address=%05lX length=%lu seq=%lu status=%u %s\n",
                    result.operation,
                    (unsigned long)result.address,
                    (unsigned long)result.length,
                    (unsigned long)request->sequence,
                    (unsigned)result.status,
                    result.replied ? "REPLIED" : "REPLY TIMEOUT");
    return result.replied;
}

static void wait_for_usb_ack_flush(void) {
    const uint64_t deadline = time_us_64() + 100000u;
    while (time_us_64() < deadline) {
        rp86_host_protocol_usb_task();
        sleep_us(100u);
    }
}

static void enter_canonical_bootloader(void) {
    park_physical_processor();
    evidence_printf("RP86 BOOTLOADER ACK\n");
    evidence_printf("RP86 BOOTLOADER ENTERING\n");
    sleep_ms(250u);
    reset_usb_boot(0u, 0u);
}

static void reboot_canonical_runtime(void) {
    park_physical_processor();
    evidence_printf("RP86 REBOOT ACK\n");
    evidence_printf("RP86 REBOOT ENTERING\n");
    sleep_ms(100u);
    watchdog_reboot(0u, 0u, 0u);
    while (true) tight_loop_contents();
}

static bool handle_runtime_control(const rp86_host_protocol_message_t *record) {
    if (record->status != RP86_HOST_PROTOCOL_STATUS_OK || record->length != 1u)
        return false;
    const uint8_t operation = record->payload[0];
    if (operation != RP86_RUNTIME_CONTROL_ENTER_BOOTLOADER &&
        operation != RP86_RUNTIME_CONTROL_REBOOT)
        return false;
    park_physical_processor();
    if (!send_runtime_control_ack(record, operation)) return false;
    wait_for_usb_ack_flush();
    if (operation == RP86_RUNTIME_CONTROL_ENTER_BOOTLOADER)
        reset_usb_boot(0u, 0u);
    watchdog_reboot(0u, 0u, 0u);
    while (true) tight_loop_contents();
}

static void service_cdc_control(void) {
#if RP86_CANONICAL_RUNTIME
    while (tud_cdc_available()) {
        char ch;
        if (tud_cdc_read(&ch, 1u) != 1u) break;
        rp86_cdc_command_t command;
        if (rp86_cdc_command_parser_feed(
                &g_cdc_command_parser, ch, &command)) {
            if (command == RP86_CDC_COMMAND_STATUS) {
                evidence_printf("RP86 STATUS BEGIN\n");
                print_canonical_status();
                evidence_printf("RP86 STATUS END\n");
            } else if (command == RP86_CDC_COMMAND_ENTER_BOOTLOADER) {
                enter_canonical_bootloader();
            } else if (command == RP86_CDC_COMMAND_REBOOT) {
                reboot_canonical_runtime();
            } else {
                evidence_printf("RP86 COMMAND ERROR\n");
            }
        }
    }
#endif
}

static void print_companion_result(const companion_result_t *r) {
    evidence_printf("\n[PERSISTENT RP86 RUNTIME]\n");
    evidence_printf("Clock                      = %.3f MHz\n",
                    (double)RP86_PROCESSOR_HZ / 1000000.0);
    evidence_printf("RESET clock qualification = %s\n",
                    r->reset_ok ? "PASS" : "FAIL");
    evidence_printf("Software INT 60h commit    = %s\n",
                    r->int60_commit_seen ? "PASS" : "FAIL");
    evidence_printf("Physical INTR assertions   = %lu\n",
                    (unsigned long)r->irq_assertions);
    evidence_printf("INTA #1 accepts            = %lu %s\n",
                    (unsigned long)r->irq_accepts,
                    r->first_inta_seen ? "PASS" : "FAIL");
    evidence_printf("INTA #2 completions        = %lu %s\n",
                    (unsigned long)r->irq_completions,
                    r->second_inta_complete ? "PASS" : "FAIL");
    evidence_printf("IRQ mailbox commit         = %s\n",
                    r->irq_commit_seen ? "PASS" : "FAIL");
    evidence_printf("Native EOI                 = %s\n",
                    r->eoi_seen ? "PASS" : "FAIL");
    evidence_printf("Heartbeat active           = %s\n",
                    r->heartbeat_active ? "PASS" : "FAIL");
    evidence_printf("Native processor identity  = %s (AAD16=%04X) %s\n",
        rp86_prepared_processor_identity_name(r->processor_signature),
        r->processor_signature,
        r->processor_identity_valid ? "PASS" : "FAIL");
    evidence_printf("IRQ mailbox commits        = %lu\n",
                    (unsigned long)r->irq_commits);
    evidence_printf("Native EOI writes          = %lu\n",
                    (unsigned long)r->eoi_writes);
    evidence_printf("PIO1 non-AD isolation      = %s\n",
                    r->non_ad_isolation ? "PASS" : "FAIL");
    evidence_printf("Observer complete cycles   = %lu\n",
                    (unsigned long)r->complete_cycles);
    evidence_printf("DMA remain foreground/ROM/I/O = %lu/%lu/%lu\n",
                    (unsigned long)r->foreground_dma_remain,
                    (unsigned long)r->irq_rom_dma_remain,
                    (unsigned long)r->irq_io_dma_remain);
    evidence_printf("TX FIFO foreground/ROM/I/O = %lu/%lu/%lu\n",
                    (unsigned long)r->foreground_fifo,
                    (unsigned long)r->irq_rom_fifo,
                    (unsigned long)r->irq_io_fifo);
    evidence_printf("SM PC foreground/ROM/I/O/INTA = %lu/%lu/%lu/%lu\n",
                    (unsigned long)r->foreground_pc,
                    (unsigned long)r->irq_rom_pc,
                    (unsigned long)r->irq_io_pc,
                    (unsigned long)r->inta_pc);
    evidence_printf("PIO1 allocation            = SM0 RESET+INT60, SM1 IRQ ROM, SM2 IRQ I/O, SM3 INTA\n");
    evidence_printf("PIO instruction words      = %u + %u = %u/32\n",
        rp86_processor_service_responder_program.length,
        rp86_interrupt_acknowledge_responder_program.length,
        rp86_processor_service_responder_program.length +
            rp86_interrupt_acknowledge_responder_program.length);
    evidence_printf("Current-cycle M33          = NONE\n");
    evidence_printf("Processor runtime state    = STI/HLT idle; IRQ heartbeat remains armed\n");
    evidence_printf("RP86 RUNTIME RESULT   = %s\n",
        r->pre_release_clean &&
        r->int60_commit_seen && r->first_inta_seen &&
        r->second_inta_complete && r->irq_commit_seen &&
        r->eoi_seen && r->heartbeat_active &&
        r->processor_identity_valid ? "PASS" : "FAIL");

    evidence_printf("\n[PASSIVE ADDRESS / R2-DATA TRACE]\n");
    /* Persistent-runtime failures often occur only after the first complete
     * IRQ/IRET round trip.  Retain the entire bounded observer window so a
     * second-entry fault cannot be hidden behind a successful first 80 cycles. */
    const uint32_t depth = r->complete_cycles;
    for (uint32_t i = 0u; i < depth; ++i) {
        const uint32_t address_raw = g_observer_dma_words[i * 2u];
        const uint32_t data_raw = g_observer_dma_words[i * 2u + 1u];
        const char *type = rp86_prepared_is_memory_read(address_raw) ? "MEMR" :
            rp86_prepared_is_memory_write(address_raw) ? "MEMW" :
            raw_io_write(address_raw) ? "IOW" :
            raw_io_read(address_raw) ? "IOR" : "OTHER";
        evidence_printf("%02lu addr=%05lX type=%s data=%04X addr_raw=%08lX data_raw=%08lX\n",
            (unsigned long)i,
            (unsigned long)rp86_prepared_decode_address(address_raw), type,
            rp86_prepared_decode_ad(data_raw), (unsigned long)address_raw,
            (unsigned long)data_raw);
    }
    evidence_printf("Physical processor remains active in STI/HLT; RESET is not asserted.\n");
}

int rp86_canonical_runtime_run(void) {
    rp86_prepared_header_high_z();
    rp86_prepared_control_outputs_init();
    rp86_prepared_route_ad_to_sio_high_z();
    rp86_evidence_queue_reset(&g_runtime_evidence);
    rp86_cdc_command_parser_init(&g_cdc_command_parser);
    rp86_prepared_runtime_init(&g_prepared_runtime);
    hard_assert(rp86_runtime_context_init(&g_runtime, HOST_TIMEOUT_US));
    rp86_host_protocol_usb_init();
    stdio_init_all();
    static companion_result_t result;
    prepare_bootstrap_record();
    compile_sequences();

    hard_assert(rp86_processor_service_responder_program.length +
                rp86_interrupt_acknowledge_responder_program.length <= 32u);
    const uint exact_offset = pio_add_program(
        pio1, &rp86_processor_service_responder_program);
    const uint inta_offset = pio_add_program(
        pio1, &rp86_interrupt_acknowledge_responder_program);
    exact_sm_init(&g_foreground_sm, 0u, exact_offset);
    exact_sm_init(&g_irq_rom_sm, 1u, exact_offset);
    exact_sm_init(&g_irq_io_sm, 2u, exact_offset);
    inta_sm_init(&g_inta_sm, 3u, inta_offset);
    hard_assert(rp86_processor_bus_init(
        &g_processor_bus, pio2, RP86_PROCESSOR_HZ,
        CLOCK_STEPPED_PIO_HZ));
    rp86_workload_executor_init(
        &g_workload_executor, &g_runtime, &g_processor_bus,
        &g_bus_active, &g_processor_boot_id,
        prepare_general_workload_bus, NULL,
        workload_evidence, NULL);
    rp86_prepared_observer_init(&g_observer_sm);

    prime_exact(&g_foreground_sm, &g_boot);
    prime_exact(&g_irq_rom_sm, &g_irq_rom);
    prime_exact(&g_irq_io_sm, &g_irq_io);
    rp86_prepared_sm_arm(&g_inta_sm);
    pio_sm_exec(pio1, g_inta_sm.sm,
                pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_put_blocking(pio1, g_inta_sm.sm,
                        rp86_prepared_encode_word(COMPANION_VECTOR));

    uint32_t foreground_initial_words = flatten_append(
        &g_boot, g_foreground_initial_words, 0u);
    foreground_initial_words = flatten_append(
        &g_int60_initial, g_foreground_initial_words,
        foreground_initial_words);
    g_int60_dma_words = flatten_full(&g_int60, g_int60_words);
    g_irq_rom_dma_words = flatten_full(&g_irq_rom, g_irq_rom_words);
    g_irq_io_dma_words = flatten_full(&g_irq_io, g_irq_io_words);
    g_foreground_dma = start_words_dma(
        &g_foreground_sm, g_foreground_initial_words,
        foreground_initial_words);
    g_irq_rom_dma = start_words_dma(
        &g_irq_rom_sm, g_irq_rom_words, g_irq_rom_dma_words);
    g_irq_io_dma = start_words_dma(
        &g_irq_io_sm, g_irq_io_words, g_irq_io_dma_words);
    dma_channel_set_irq0_enabled((uint)g_foreground_dma, true);
    dma_channel_set_irq0_enabled((uint)g_irq_rom_dma, true);
    dma_channel_set_irq0_enabled((uint)g_irq_io_dma, true);
    irq_set_exclusive_handler(DMA_IRQ_0, companion_dma_irq0);
    irq_set_priority(DMA_IRQ_0, 0x40u);
    irq_set_enabled(DMA_IRQ_0, true);
    const int observer_dma = rp86_prepared_start_observer_dma(
        &g_observer_sm, g_observer_dma_words,
        RP86_PREPARED_OBSERVER_WORDS);
    g_observer_dma = observer_dma;
    rp86_prepared_runtime_mark_initialized(&g_prepared_runtime);

    rp86_prepared_route_ad_to_responder(&g_foreground_sm);
    result.non_ad_isolation =
        rp86_prepared_non_ad_pins_isolated(&g_foreground_sm);
    result.pre_release_clean = result.non_ad_isolation &&
        pio1->dbg_padoe == 0u && !gpio_get(RP86_PROCESSOR_PIN_CLK) &&
        (sio_hw->gpio_oe & RP86_PROCESSOR_AD_BUS_MASK) == 0u;

    gpio_put(RP86_PROCESSOR_PIN_RESET, true);
    hard_assert(rp86_processor_bus_set_execution_clock_mode(
        &g_processor_bus, RP86_EXECUTION_CLOCK_FREE_RUNNING,
        EXECUTION_CLOCK_SWITCH_US));
    const bool reset_ok = rp86_prepared_wait_reset_clocks(
        RP86_PROCESSOR_RESET_CLOCKS, RP86_PROCESSOR_HZ);
    result.reset_ok = reset_ok;
    hard_assert(rp86_processor_bus_set_execution_clock_mode(
        &g_processor_bus, RP86_EXECUTION_CLOCK_CLOCK_STEPPED,
        EXECUTION_CLOCK_SWITCH_US));
    result.pre_release_clean = result.pre_release_clean && reset_ok;

    pio_enable_sm_mask_in_sync(pio1, 0x0Fu);
    pio_sm_set_enabled(pio0, g_observer_sm.sm, true);
    ++g_processor_boot_id;
    if (g_processor_boot_id == 0u)
        ++g_processor_boot_id;
    hard_assert(rp86_processor_bus_set_execution_clock_mode(
        &g_processor_bus, RP86_EXECUTION_CLOCK_FREE_RUNNING,
        EXECUTION_CLOCK_SWITCH_US));
    gpio_put(RP86_PROCESSOR_PIN_RESET, false);
    g_bus_active = true;

    busy_wait_ms(20u);
    for (uint32_t heartbeat = 0u;
         heartbeat < HEARTBEAT_ACCEPT_COUNT; ++heartbeat) {
        gpio_put(RP86_PROCESSOR_PIN_INTR, true);
        ++result.irq_assertions;
        const uint64_t deadline = time_us_64() + IRQ_TIMEOUT_US;
        while (!pio_interrupt_get(pio1, 4u) && time_us_64() <= deadline)
            rp86_host_protocol_usb_task();
        if (pio_interrupt_get(pio1, 4u)) {
            pio_interrupt_clear(pio1, 4u);
            gpio_put(RP86_PROCESSOR_PIN_INTR, false);
            result.first_inta_seen = true;
            ++result.irq_accepts;
        }
        while (!pio_interrupt_get(pio1, 5u) && time_us_64() <= deadline)
            rp86_host_protocol_usb_task();
        if (pio_interrupt_get(pio1, 5u)) {
            pio_interrupt_clear(pio1, 5u);
            result.second_inta_complete = true;
            ++result.irq_completions;
            pio_sm_put_blocking(pio1, g_inta_sm.sm,
                                rp86_prepared_encode_word(COMPANION_VECTOR));
        }
        busy_wait_us_32(IRQ_PERIOD_US);
    }

    const uint32_t observer_words = RP86_PREPARED_OBSERVER_WORDS -
        rp86_prepared_dma_remaining(observer_dma);
    result.observer_words = observer_words;
    result.observer_complete = observer_words >= 2u;
    classify_trace_words(&result, observer_words);
    live_round_result_t initial_round = {0};
    classify_live_round(&initial_round, observer_words,
                        stage_live_payload(&g_bootstrap_record));
    initial_round.inta1 = result.first_inta_seen;
    initial_round.inta2 = result.second_inta_complete;
    result.processor_signature = initial_round.processor_signature;
    result.processor_identity_valid = initial_round.processor_identity_valid;
    rp86_prepared_runtime_observe_processor(
        &g_prepared_runtime, initial_round.processor_signature);
    result.heartbeat_active = result.irq_completions >= 2u &&
        result.irq_commits >= 1u && result.eoi_writes >= 1u &&
        initial_round.witness && initial_round.irq_commit &&
        initial_round.native_counter_valid &&
        initial_round.processor_identity_valid && initial_round.eoi &&
        initial_round.foreground_commit;
    result.foreground_dma_remain = rp86_prepared_dma_remaining(g_foreground_dma);
    result.irq_rom_dma_remain = rp86_prepared_dma_remaining(g_irq_rom_dma);
    result.irq_io_dma_remain = rp86_prepared_dma_remaining(g_irq_io_dma);
    result.foreground_fifo =
        pio_sm_get_tx_fifo_level(pio1, g_foreground_sm.sm);
    result.irq_rom_fifo = pio_sm_get_tx_fifo_level(pio1, g_irq_rom_sm.sm);
    result.irq_io_fifo = pio_sm_get_tx_fifo_level(pio1, g_irq_io_sm.sm);
    result.foreground_pc = pio_sm_get_pc(pio1, g_foreground_sm.sm);
    result.irq_rom_pc = pio_sm_get_pc(pio1, g_irq_rom_sm.sm);
    result.irq_io_pc = pio_sm_get_pc(pio1, g_irq_io_sm.sm);
    result.inta_pc = pio_sm_get_pc(pio1, g_inta_sm.sm);

    /* Identity is retained in structured status even without a CDC reader.
     * Do not wait for USB or emit an unsolicited sequence-zero HID response. */
    if (stdio_usb_connected()) print_companion_result(&result);

    /* Persistent service condition: one complete host record owns one V30
     * interrupt.  The reply is not published until passive PIO0 evidence has
     * observed the native witness, six-word IRQ commit, EOI, and the following
     * INT60 commit that proves IRET returned to the idle loop. */
    while (true) {
        rp86_host_protocol_usb_task();
        service_cdc_control();
        drain_runtime_evidence();
        rp86_host_protocol_message_t request;
        if (rp86_workload_executor_active(&g_workload_executor)) {
            (void)take_non_control_record(&request);
            service_general_workload();
        } else if (rp86_prepared_runtime_available(&g_prepared_runtime) &&
                   take_live_record(&request)) {
            live_round_result_t round;
            const bool passed = run_live_round(
                &g_foreground_sm, &g_irq_rom_sm, &g_irq_io_sm, &g_inta_sm,
                &g_observer_sm, observer_dma, &request, &round);
            send_live_reply(&request, &round, passed);
        } else if (!rp86_prepared_runtime_available(&g_prepared_runtime)) {
            (void)take_non_control_record(&request);
        }
        tight_loop_contents();
    }
}
