/*
 * AI-B1-A bounded runtime-staged physical mailbox at 0.600 MHz.
 *
 * This translation unit reuses the accepted C0C0 decode, observer, safety,
 * ROM-table and reporting helpers. It supplies a new run topology: PIO2 owns
 * CLK, while all four PIO1 SMs form independent ROM and mailbox
 * matcher/responder pairs using the shared 24-word relative-IRQ program.
 */

#define PC1C0C_V30_HZ 600000u
#define SEQUENCE_MAX 96u
#define main pc1c0c_legacy_main_not_used
#include "pc1c_sram_rom_execution.c"
#undef main

#include "pico/multicore.h"
#include "ai_bridge/bridge_protocol.h"
#include "runtime/spsc_u32_ring.h"
#include "pc1c_dual_sequencer.pio.h"

#define MAILBOX_RX_PORT              0x00E4u
#define MAILBOX_TX_PORT              0x00E2u
#define MAILBOX_CONTROL_PORT         0x00E6u
#define MAILBOX_CHECKSUM_PORT        0x00E8u
#define MAILBOX_WORDS                      7u
#define REPLY_WORDS                        9u
#define BRIDGE_RECORD_WORDS \
    (PI86_BRIDGE_MESSAGE_SIZE / sizeof(uint32_t))
#define STAGING_TIMEOUT_US            500000u

static const uint16_t expected_mailbox_words[MAILBOX_WORDS] = {
    0x4548u, 0x4C4Cu, 0x204Fu, 0x454Eu, 0x2043u, 0x3356u, 0x0030u,
};
static const char expected_reply[] = "HELLO OPENAI CODEX";

static pi86_spsc_u32_ring_t g_message_ring;
static pi86_bridge_message_t g_staged_message;
static uint32_t g_mailbox_keys[MAILBOX_WORDS];
static uint32_t g_mailbox_responses[MAILBOX_WORDS];

typedef struct {
    pc1c0c_result_t base;
    bool core1_record_complete;
    bool core0_record_valid;
    bool staging_atomic;
    bool mailbox_reads_ok;
    bool checksum_ok;
    bool reply_ok;
    bool commit_ok;
    bool key_sets_disjoint;
    bool mailbox_dma_complete;
    uint32_t mailbox_reads;
    uint32_t mailbox_mismatches;
    uint32_t reply_bytes;
    uint32_t rom_pairs;
    uint32_t mailbox_pairs;
    uint32_t mailbox_key_dma_pre;
    uint32_t mailbox_response_dma_pre;
    uint32_t mailbox_key_dma_post;
    uint32_t mailbox_response_dma_post;
    uint32_t mailbox_matcher_fifo_pre;
    uint32_t mailbox_responder_fifo_pre;
    uint32_t pre_pio2_padoe;
    char reply[sizeof(expected_reply)];
} ai_b1_result_t;

static void service_core_publish_record(void) {
    pi86_bridge_message_t message = {0};
    message.version = PI86_BRIDGE_PROTOCOL_VERSION;
    message.type = PI86_BRIDGE_MESSAGE_HELLO;
    message.sequence = 1u;
    message.length = 13u;
    memcpy(message.payload, "HELLO NEC V30", message.length);

    for (uint32_t i = 0u; i < BRIDGE_RECORD_WORDS; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&message + i * sizeof word,
               sizeof word);
        while (!pi86_spsc_u32_try_push(&g_message_ring, word))
            tight_loop_contents();
    }
    while (true) tight_loop_contents();
}

static bool stage_complete_record(ai_b1_result_t *r) {
    multicore_launch_core1(service_core_publish_record);
    const uint64_t deadline = time_us_64() + STAGING_TIMEOUT_US;
    while ((g_message_ring.write_index - g_message_ring.read_index) <
               BRIDGE_RECORD_WORDS &&
           time_us_64() <= deadline)
        tight_loop_contents();

    r->core1_record_complete =
        (g_message_ring.write_index - g_message_ring.read_index) ==
        BRIDGE_RECORD_WORDS;
    if (!r->core1_record_complete) return false;

    pi86_bridge_message_t candidate = {0};
    for (uint32_t i = 0u; i < BRIDGE_RECORD_WORDS; ++i) {
        uint32_t word = 0u;
        if (!pi86_spsc_u32_try_pop(&g_message_ring, &word)) return false;
        memcpy((uint8_t *)&candidate + i * sizeof word, &word, sizeof word);
    }

    r->core0_record_valid =
        candidate.version == PI86_BRIDGE_PROTOCOL_VERSION &&
        candidate.type == PI86_BRIDGE_MESSAGE_HELLO &&
        candidate.length == 13u &&
        memcmp(candidate.payload, "HELLO NEC V30", 13u) == 0;
    if (!r->core0_record_valid) return false;

    memcpy(&g_staged_message, &candidate, sizeof candidate);
    __dmb();
    r->staging_atomic =
        g_message_ring.read_index == g_message_ring.write_index;
    return r->staging_atomic;
}

static bool is_io_read(uint32_t raw) {
    return sample_bit(raw, V30_PIN_IOM) == 0u &&
           sample_bit(raw, V30_PIN_BUFRW) == 0u &&
           sample_bit(raw, V30_PIN_INTAK) != 0u;
}

static uint32_t qualified_io_read_key(uint16_t port) {
    return (1u << V30_PIN_ASTB) | (1u << V30_PIN_INTAK) |
           encode_gpio_address(port);
}

static void prepare_mailbox_tables(ai_b1_result_t *r) {
    uint8_t bytes[MAILBOX_WORDS * 2u] = {0};
    memcpy(bytes, g_staged_message.payload, g_staged_message.length);
    for (uint32_t i = 0u; i < MAILBOX_WORDS; ++i) {
        const uint16_t word = (uint16_t)bytes[i * 2u] |
            ((uint16_t)bytes[i * 2u + 1u] << 8);
        g_mailbox_keys[i] = qualified_io_read_key(MAILBOX_RX_PORT);
        g_mailbox_responses[i] = encoded_drive_command(word);
    }

    r->key_sets_disjoint = true;
    for (uint32_t i = 0u; i < g_sequence_count; ++i)
        for (uint32_t j = 0u; j < MAILBOX_WORDS; ++j)
            if (g_sequence_keys[i] == g_mailbox_keys[j])
                r->key_sets_disjoint = false;
}

static void init_sm_at(PIO pio, uint sm, uint offset,
                       const struct pio_program *program, bool responder) {
    pio_sm_claim(pio, sm);
    pio_sm_config c = responder ?
        pc1c_dual_responder_program_get_default_config(offset) :
        pc1c_dual_matcher_program_get_default_config(offset);
    if (responder) {
        sm_config_set_out_pins(&c, OUT_BASE, OUT_COUNT);
        sm_config_set_out_shift(&c, true, false, 32u);
    } else {
        sm_config_set_in_pins(&c, 0u);
    }
    hard_assert(pio_sm_init(pio, sm, offset, &c) == PICO_OK);
    pio_sm_set_enabled(pio, sm, false);
    (void)program;
}

static void init_response_plane(pc1c_sm_t *rom_matcher,
                                pc1c_sm_t *rom_responder,
                                pc1c_sm_t *mailbox_matcher,
                                pc1c_sm_t *mailbox_responder) {
    hard_assert(pc1c_dual_matcher_program.length +
                pc1c_dual_responder_program.length <= 32u);
    const uint matcher_offset = pio_add_program(
        pio1, &pc1c_dual_matcher_program);
    const uint responder_offset = pio_add_program(
        pio1, &pc1c_dual_responder_program);

    *rom_matcher = (pc1c_sm_t){pio1, 0u, matcher_offset};
    *rom_responder = (pc1c_sm_t){pio1, 1u, responder_offset};
    *mailbox_matcher = (pc1c_sm_t){pio1, 2u, matcher_offset};
    *mailbox_responder = (pc1c_sm_t){pio1, 3u, responder_offset};

    init_sm_at(pio1, 0u, matcher_offset, &pc1c_dual_matcher_program, false);
    init_sm_at(pio1, 1u, responder_offset,
               &pc1c_dual_responder_program, true);
    init_sm_at(pio1, 2u, matcher_offset, &pc1c_dual_matcher_program, false);
    init_sm_at(pio1, 3u, responder_offset,
               &pc1c_dual_responder_program, true);
}

static void init_clock_on_pio2(pc1c_sm_t *clock) {
    clock->pio = pio2;
    clock->sm = pio_claim_unused_sm(clock->pio, true);
    clock->offset = pio_add_program(clock->pio, &perf_continuous_clk_program);
}

static bool stream_empty(int dma, const pc1c_sm_t *sm) {
    return dma_remaining(dma) == 0u &&
           pio_sm_is_tx_fifo_empty(sm->pio, sm->sm);
}

static void classify_mailbox(ai_b1_result_t *r) {
    for (uint32_t i = 0u; i < r->base.trace_count; ++i) {
        const bus_trace_t *entry = &r->base.trace[i];
        const uint32_t address = decode_address(entry->address_raw);
        const uint16_t data = decode_ad(entry->data_raw);
        if (is_io_read(entry->address_raw) && address == MAILBOX_RX_PORT &&
            entry->lanes == LANES_WORD) {
            if (r->mailbox_reads < MAILBOX_WORDS &&
                data != expected_mailbox_words[r->mailbox_reads])
                ++r->mailbox_mismatches;
            ++r->mailbox_reads;
        }
        if (entry->io_write && address == MAILBOX_CHECKSUM_PORT &&
            entry->lanes == LANES_WORD && data == 0u)
            r->checksum_ok = true;
        if (entry->io_write && address == MAILBOX_TX_PORT &&
            entry->lanes == LANES_WORD &&
            r->reply_bytes + 2u < sizeof r->reply) {
            r->reply[r->reply_bytes++] = (char)(data & 0xFFu);
            r->reply[r->reply_bytes++] = (char)(data >> 8);
        }
        if (entry->io_write && address == MAILBOX_CONTROL_PORT &&
            entry->lanes == LANES_WORD && data == 1u)
            r->commit_ok = true;
    }
    r->mailbox_reads_ok = r->mailbox_reads == MAILBOX_WORDS &&
        r->mailbox_mismatches == 0u;
    r->reply[r->reply_bytes] = '\0';
    r->reply_ok = r->reply_bytes == sizeof(expected_reply) - 1u &&
        memcmp(r->reply, expected_reply, sizeof(expected_reply) - 1u) == 0;
}

static void run_ai_b1(pc1c_sm_t *clock, pc1c_sm_t *rom_matcher,
                      pc1c_sm_t *rom_responder,
                      pc1c_sm_t *mailbox_matcher,
                      pc1c_sm_t *mailbox_responder, pc1c_sm_t *phase,
                      pc1c_sm_t *observer, ai_b1_result_t *r) {
    pc1c0c_result_t *b = &r->base;
    memset(b, 0, sizeof *b);
    memset(g_observer_dma_words, 0, sizeof g_observer_dma_words);
    gpio_put(V30_PIN_RESET, true);
    route_ad_to_sio_high_z();

    clock_start(clock);
    b->reset_ok = wait_reset_clocks(RESET_CLOCKS);
    clock_stop_low(clock);
    clock_prepare(clock);

    arm_sm(rom_matcher);
    arm_sm(rom_responder);
    arm_sm(mailbox_matcher);
    arm_sm(mailbox_responder);
    arm_sm(phase);
    arm_sm(observer);
    pio_interrupt_clear(pio1, 0u);
    pio_interrupt_clear(pio1, 2u);
    pio_sm_exec(pio1, rom_responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_exec(pio1, mailbox_responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));

    const int rom_key_dma = start_pio_tx_dma(
        rom_matcher, g_sequence_keys, g_sequence_count);
    const int rom_response_dma = start_pio_tx_dma(
        rom_responder, g_sequence_responses, g_sequence_count);
    const int mailbox_key_dma = start_pio_tx_dma(
        mailbox_matcher, g_mailbox_keys, MAILBOX_WORDS);
    const int mailbox_response_dma = start_pio_tx_dma(
        mailbox_responder, g_mailbox_responses, MAILBOX_WORDS);

    const bool rom_matcher_primed = wait_fifo_primed(rom_matcher, 4u);
    const bool rom_responder_primed = wait_fifo_primed(rom_responder, 4u);
    const bool mailbox_matcher_primed =
        wait_fifo_primed(mailbox_matcher, 4u);
    const bool mailbox_responder_primed =
        wait_fifo_primed(mailbox_responder, 4u);

    b->matcher_fifo_pre = pio_sm_get_tx_fifo_level(pio1, rom_matcher->sm);
    b->responder_fifo_pre = pio_sm_get_tx_fifo_level(pio1, rom_responder->sm);
    r->mailbox_matcher_fifo_pre =
        pio_sm_get_tx_fifo_level(pio1, mailbox_matcher->sm);
    r->mailbox_responder_fifo_pre =
        pio_sm_get_tx_fifo_level(pio1, mailbox_responder->sm);
    b->matcher_dma_pre = dma_remaining(rom_key_dma);
    b->responder_dma_pre = dma_remaining(rom_response_dma);
    r->mailbox_key_dma_pre = dma_remaining(mailbox_key_dma);
    r->mailbox_response_dma_pre = dma_remaining(mailbox_response_dma);

    route_ad_to_responder(rom_responder);
    b->pre_pio1_padoe = pio1->dbg_padoe;
    r->pre_pio2_padoe = pio2->dbg_padoe;
    b->clock_direction_armed = b->pre_pio1_padoe == 0u &&
        (r->pre_pio2_padoe & (1u << V30_PIN_CLK)) != 0u &&
        (r->pre_pio2_padoe & ~((uint32_t)1u << V30_PIN_CLK)) == 0u;

    const int observer_dma = start_observer_dma(observer);
    b->observer_dma_pre = dma_remaining(observer_dma);
    b->pre_release_clean = rom_matcher_primed && rom_responder_primed &&
        mailbox_matcher_primed && mailbox_responder_primed &&
        b->matcher_fifo_pre == 4u && b->responder_fifo_pre == 4u &&
        r->mailbox_matcher_fifo_pre == 4u &&
        r->mailbox_responder_fifo_pre == 4u &&
        b->observer_dma_pre == OBSERVER_WORDS &&
        b->clock_direction_armed && r->key_sets_disjoint &&
        !gpio_get(V30_PIN_CLK) &&
        (sio_hw->gpio_oe & V30_AD_BUS_MASK) == 0u;

    pio_enable_sm_mask_in_sync(pio0,
        (1u << phase->sm) | (1u << observer->sm));
    pio_enable_sm_mask_in_sync(pio1, 0x0Fu);

    if (b->reset_ok && b->pre_release_clean) {
        const uint32_t irq_state = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_sm_set_enabled(clock->pio, clock->sm, true);
        const uint64_t deadline =
            time_us_64() + timeout_us_from_clocks(RUN_TIMEOUT_CLOCKS);
        while ((!stream_empty(rom_response_dma, rom_responder) ||
                !stream_empty(mailbox_response_dma, mailbox_responder)) &&
               time_us_64() <= deadline)
            tight_loop_contents();
        if (stream_empty(rom_response_dma, rom_responder) &&
            stream_empty(mailbox_response_dma, mailbox_responder))
            busy_wait_us_32((uint32_t)timeout_us_from_clocks(2u));
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
        restore_interrupts(irq_state);
    } else {
        gpio_put(V30_PIN_RESET, true);
        clock_stop_low(clock);
    }

    b->matcher_dma_post = dma_remaining(rom_key_dma);
    b->responder_dma_post = dma_remaining(rom_response_dma);
    r->mailbox_key_dma_post = dma_remaining(mailbox_key_dma);
    r->mailbox_response_dma_post = dma_remaining(mailbox_response_dma);
    b->matcher_fifo_post = pio_sm_get_tx_fifo_level(pio1, rom_matcher->sm);
    b->responder_fifo_post = pio_sm_get_tx_fifo_level(pio1, rom_responder->sm);
    const uint32_t rom_remaining = b->responder_dma_post +
        b->responder_fifo_post;
    const uint32_t mailbox_remaining = r->mailbox_response_dma_post +
        pio_sm_get_tx_fifo_level(pio1, mailbox_responder->sm);
    r->rom_pairs = rom_remaining <= g_sequence_count ?
        g_sequence_count - rom_remaining : 0u;
    r->mailbox_pairs = mailbox_remaining <= MAILBOX_WORDS ?
        MAILBOX_WORDS - mailbox_remaining : 0u;
    b->qualified_pairs = r->rom_pairs;
    b->dma_streams_complete = b->matcher_dma_post == 0u &&
        b->responder_dma_post == 0u;
    r->mailbox_dma_complete = r->mailbox_key_dma_post == 0u &&
        r->mailbox_response_dma_post == 0u;

    pio_sm_set_enabled(pio1, rom_matcher->sm, false);
    pio_sm_set_enabled(pio1, rom_responder->sm, false);
    pio_sm_set_enabled(pio1, mailbox_matcher->sm, false);
    pio_sm_set_enabled(pio1, mailbox_responder->sm, false);
    pio_sm_set_enabled(pio0, phase->sm, false);
    pio_sm_set_enabled(pio0, observer->sm, false);
    pio_sm_exec(pio1, rom_responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_exec(pio1, mailbox_responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));

    while (!pio_sm_is_rx_fifo_empty(pio0, phase->sm) &&
           b->phase_count < FIRST_PHASE_COUNT)
        b->phase_raw[b->phase_count++] = pio_sm_get(pio0, phase->sm);
    for (uint spin = 0u; spin < 4096u &&
         !pio_sm_is_rx_fifo_empty(pio0, observer->sm); ++spin)
        tight_loop_contents();
    b->observer_fifo_residue =
        pio_sm_get_rx_fifo_level(pio0, observer->sm);
    b->observer_dma_post = dma_remaining(observer_dma);
    b->observer_words = OBSERVER_WORDS - b->observer_dma_post;
    b->observer_trailing_words = b->observer_words & 1u;
    b->observer_tail_valid = b->observer_trailing_words == 0u ||
        sample_bit(g_observer_dma_words[b->observer_words - 1u],
                   V30_PIN_ASTB) != 0u;
    b->dma_observer_first_ok = b->observer_words >= 2u &&
        decode_address(g_observer_dma_words[0]) == RESET_ROM_BASE;
    b->first_address_ok = b->dma_observer_first_ok;
    b->first_memory_read = b->observer_words >= 2u &&
        is_memory_read(g_observer_dma_words[0]);
    if (b->phase_count == FIRST_PHASE_COUNT)
        b->first_response_phase_ok =
            decode_ad(b->phase_raw[3]) == 0x00EAu &&
            decode_ad(b->phase_raw[4]) == 0x00EAu &&
            decode_ad(b->phase_raw[5]) == 0x00EAu;

    classify_trace(b);
    classify_mailbox(r);

    stop_dma(rom_key_dma);
    stop_dma(rom_response_dma);
    stop_dma(mailbox_key_dma);
    stop_dma(mailbox_response_dma);
    stop_dma(observer_dma);
    route_ad_to_sio_high_z();
    b->terminal_safe = gpio_get(V30_PIN_RESET) &&
        !gpio_get(V30_PIN_CLK) && ad_is_sio_high_z();
}

static bool ai_b1_pass(const ai_b1_result_t *r) {
    const pc1c0c_result_t *b = &r->base;
    return result_valid(b) && b->first_response_phase_ok &&
        b->far_target_seen && b->checkpoint_ok &&
        b->deadline_misses == 0u && b->terminal_safe &&
        r->core1_record_complete && r->core0_record_valid &&
        r->staging_atomic && r->mailbox_reads_ok && r->checksum_ok &&
        r->reply_ok && r->commit_ok && r->key_sets_disjoint &&
        b->dma_streams_complete && r->mailbox_dma_complete &&
        r->rom_pairs == g_sequence_count &&
        r->mailbox_pairs == MAILBOX_WORDS;
}

static void print_ai_b1(const ai_b1_result_t *r) {
    const pc1c0c_result_t *b = &r->base;
    printf("\n[V30 MAILBOX OUTPUT]\n%s\n", r->reply);
    printf("\n[SUMMARY]\n");
    printf("Measurement epoch          %s\n", pass_fail(result_valid(b)));
    printf("Reset / FFFF0 fetch        %s\n",
           pass_fail(b->reset_ok && b->first_address_ok));
    printf("First response 00EA        %s\n",
           pass_fail(b->first_response_phase_ok));
    printf("F0000 ROM execution        %s\n", pass_fail(b->far_target_seen));
    printf("Core1 complete record      %s\n",
           pass_fail(r->core1_record_complete));
    printf("Core0 immutable staging    %s\n",
           pass_fail(r->core0_record_valid && r->staging_atomic));
    printf("Mailbox RX I/O 00E4        %s (%lu/%u words)\n",
           pass_fail(r->mailbox_reads_ok),
           (unsigned long)r->mailbox_reads, MAILBOX_WORDS);
    printf("V30 input XOR at 00E8      %s\n", pass_fail(r->checksum_ok));
    printf("Mailbox TX I/O 00E2        %s\n", pass_fail(r->reply_ok));
    printf("Mailbox commit I/O 00E6    %s\n", pass_fail(r->commit_ok));
    printf("ROM/mailbox key collisions %s\n",
           r->key_sets_disjoint ? "0 PASS" : "FAIL");
    printf("Current-cycle M33          NONE\n");
    printf("Bus ownership/safety       %s\n", pass_fail(b->terminal_safe));
    printf("AI-B1-A RESULT             %s\n", pass_fail(ai_b1_pass(r)));

    printf("\n[ENGINEERING DETAILS]\n");
    printf("AI-B1-A Dual-Sequence Runtime Mailbox - 0.600 MHz\n");
    printf("PIO1 allocation            = SM0/1 ROM, SM2/3 mailbox\n");
    printf("PIO instruction words      = %u + %u = %u/32\n",
           pc1c_dual_matcher_program.length,
           pc1c_dual_responder_program.length,
           pc1c_dual_matcher_program.length +
               pc1c_dual_responder_program.length);
    printf("PIO1 pre-release OE        = %08lX %s\n",
           (unsigned long)b->pre_pio1_padoe,
           pass_fail(b->pre_pio1_padoe == 0u));
    printf("PIO2 pre-release OE        = %08lX CLK-ONLY %s\n",
           (unsigned long)r->pre_pio2_padoe,
           pass_fail(b->clock_direction_armed));
    printf("ROM qualified pairs        = %lu/%lu %s\n",
           (unsigned long)r->rom_pairs,
           (unsigned long)g_sequence_count,
           pass_fail(r->rom_pairs == g_sequence_count));
    printf("Mailbox qualified pairs    = %lu/%u %s\n",
           (unsigned long)r->mailbox_pairs, MAILBOX_WORDS,
           pass_fail(r->mailbox_pairs == MAILBOX_WORDS));
    printf("ROM DMA remain key/response= %lu/%lu\n",
           (unsigned long)b->matcher_dma_post,
           (unsigned long)b->responder_dma_post);
    printf("Mailbox DMA remain key/resp= %lu/%lu\n",
           (unsigned long)r->mailbox_key_dma_post,
           (unsigned long)r->mailbox_response_dma_post);
    printf("Response deadline misses   = %lu %s\n",
           (unsigned long)b->deadline_misses,
           pass_fail(b->deadline_misses == 0u));
    printf("Observer complete cycles   = %u\n", b->trace_count);
    printf("ROM image                  = %lu bytes; SHA-256 %s\n",
           (unsigned long)PC1C_ROM_SIZE, PC1C_ROM_SHA256);
    printf("TERMINAL SAFE STATE        = %s\n",
           pass_fail(b->terminal_safe));
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    prepare_response_tables();
    prepare_header_high_z();
    init_control_outputs();
    route_ad_to_sio_high_z();
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(100);

    static ai_b1_result_t result;
    memset(&result, 0, sizeof result);
    if (!stage_complete_record(&result)) {
        print_ai_b1(&result);
        while (true) tight_loop_contents();
    }
    prepare_mailbox_tables(&result);

    pc1c_sm_t clock, rom_matcher, rom_responder;
    pc1c_sm_t mailbox_matcher, mailbox_responder, phase, observer;
    init_clock_on_pio2(&clock);
    init_response_plane(&rom_matcher, &rom_responder,
                        &mailbox_matcher, &mailbox_responder);
    phase_capture_init(&phase);
    observer_init(&observer);

    run_ai_b1(&clock, &rom_matcher, &rom_responder,
              &mailbox_matcher, &mailbox_responder,
              &phase, &observer, &result);
    print_ai_b1(&result);
    fflush(stdout);
    while (true) tight_loop_contents();
}
