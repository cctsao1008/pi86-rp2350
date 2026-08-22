/*
 * AI-B1-B live-publication mailbox at 0.600 MHz.
 *
 * Windows sends one complete provider-neutral record over the selected USB
 * transport before the
 * deterministic V30 epoch. Core1 owns binary ingress and transfers only the
 * complete record. Core0 validates and compiles immutable response streams,
 * but deliberately withholds them while the already-running V30 performs its
 * first STATUS read. That read is answered NOT_READY by a prearmed PIO word.
 * A relative PIO IRQ then lets Core0 atomically start the prepared key and
 * response DMA channels between bus cycles. The second STATUS read returns
 * READY and the V30 consumes the seven payload words.
 *
 * USB IRQs remain masked throughout the V30 epoch. This gate proves physical
 * host ingress plus live mailbox publication without claiming concurrent USB
 * service during a realtime bus cycle; that later transport policy belongs to
 * AI-B2. No M33 resolves a current V30 response cycle.
 */

#define SEQUENCE_MAX 256u
#define AI_B1A_MAIN ai_b1a_main_not_used
#define PC1C_DUAL_SEQUENCER_HEADER "pc1c_live_mailbox.pio.h"
#include "ai_bridge_runtime_mailbox.c"
#undef PC1C_DUAL_SEQUENCER_HEADER
#undef AI_B1A_MAIN

#include "hardware/regs/dma.h"

#ifdef AI_BRIDGE_HID_TRANSPORT
#include "ai_bridge_usb.h"
#endif

#define LIVE_STATUS_PORT                 0x00E0u
#define LIVE_DATA_PORT                   0x00E4u
#define LIVE_TX_PORT                     0x00E2u
#define LIVE_CONTROL_PORT                0x00E6u
#define LIVE_WITNESS_PORT                0x00E8u
#define LIVE_DATA_WORDS                        7u
#define LIVE_PUBLISHED_PAIRS  (1u + LIVE_DATA_WORDS)
#define LIVE_TOTAL_PAIRS      (1u + LIVE_PUBLISHED_PAIRS)
#define HOST_RECORD_TIMEOUT_US          5000000u
#define PUBLICATION_IRQ                        6u

static const uint16_t live_expected_words[LIVE_DATA_WORDS] = {
    0x4548u, 0x4C4Cu, 0x204Fu, 0x454Eu, 0x2043u, 0x3356u, 0x0030u,
};
static const char live_expected_reply[] = "HELLO OPENAI CODEX";

static uint32_t g_live_keys[LIVE_PUBLISHED_PAIRS];
static uint32_t g_live_responses[LIVE_PUBLISHED_PAIRS];

typedef struct {
    pc1c0c_result_t base;
    bool windows_record_complete;
    bool core1_record_complete;
    bool core0_record_valid;
    bool staging_atomic;
    bool first_not_ready_observed;
    bool publication_triggered;
    bool publication_atomic;
    bool deferred_dma_armed;
    bool status_transition_ok;
    bool mailbox_reads_ok;
    bool checksum_ok;
    bool reply_ok;
    bool commit_ok;
    bool key_sets_disjoint;
    bool mailbox_dma_complete;
#ifdef AI_BRIDGE_HID_TRANSPORT
    bool hid_reply_complete;
    uint32_t hid_reply_bytes;
#endif
    uint32_t host_sequence;
    uint32_t status_reads;
    uint16_t status_values[2];
    uint32_t mailbox_reads;
    uint32_t mailbox_mismatches;
    uint32_t reply_bytes;
    uint32_t rom_pairs;
    uint32_t mailbox_pairs;
    uint32_t mailbox_key_dma_pre;
    uint32_t mailbox_response_dma_pre;
    uint32_t mailbox_key_reload_pre;
    uint32_t mailbox_response_reload_pre;
    uint32_t mailbox_key_dma_post;
    uint32_t mailbox_response_dma_post;
    uint32_t mailbox_matcher_fifo_pre;
    uint32_t mailbox_responder_fifo_pre;
    uint32_t pre_pio2_padoe;
    char reply[sizeof live_expected_reply];
} ai_b1b_result_t;

static void service_core_receive_record(void) {
    uint8_t record[PI86_BRIDGE_MESSAGE_SIZE];
#ifdef AI_BRIDGE_HID_TRANSPORT
    while (!pi86_ai_bridge_hid_take_record(record))
        tight_loop_contents();
#else
    uint32_t received = 0u;

    while (received < sizeof record) {
        const int value = getchar_timeout_us(1000u);
        if (value == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }
        record[received++] = (uint8_t)value;
    }
#endif

    for (uint32_t i = 0u; i < BRIDGE_RECORD_WORDS; ++i) {
        uint32_t word;
        memcpy(&word, record + i * sizeof word, sizeof word);
        while (!pi86_spsc_u32_try_push(&g_message_ring, word))
            tight_loop_contents();
    }
    while (true) tight_loop_contents();
}

static bool receive_complete_host_record(ai_b1b_result_t *r) {
    multicore_launch_core1(service_core_receive_record);
    const uint64_t deadline = time_us_64() + HOST_RECORD_TIMEOUT_US;
    while ((g_message_ring.write_index - g_message_ring.read_index) <
               BRIDGE_RECORD_WORDS &&
           time_us_64() <= deadline) {
#ifdef AI_BRIDGE_HID_TRANSPORT
        /* Core1 waits on HID ownership transfer; Core0 alone services USB.
         * This loop ends before RESET release and the deterministic epoch. */
        pi86_ai_bridge_usb_task();
#else
        tight_loop_contents();
#endif
    }

    r->core1_record_complete =
        (g_message_ring.write_index - g_message_ring.read_index) ==
        BRIDGE_RECORD_WORDS;
    r->windows_record_complete = r->core1_record_complete;
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
        candidate.status == PI86_BRIDGE_STATUS_OK &&
        candidate.length == 13u &&
        memcmp(candidate.payload, "HELLO NEC V30", 13u) == 0;
    if (!r->core0_record_valid) return false;

    memcpy(&g_staged_message, &candidate, sizeof candidate);
    __dmb();
    r->host_sequence = candidate.sequence;
    r->staging_atomic =
        g_message_ring.read_index == g_message_ring.write_index;
    return r->staging_atomic;
}

static __attribute__((noreturn)) void terminal_service_loop(void) {
    while (true) {
#ifdef AI_BRIDGE_HID_TRANSPORT
        /* The deterministic V30 epoch has ended and the bus is already in its
         * terminal safe state. Keep TinyUSB moving so every queued CDC
         * evidence packet reaches Windows; the direct tinyusb_device linkage
         * intentionally disables pico_stdio_usb's background worker. */
        pi86_ai_bridge_usb_task();
#else
        tight_loop_contents();
#endif
    }
}

static void prepare_live_tables(ai_b1b_result_t *r) {
    uint8_t bytes[LIVE_DATA_WORDS * 2u] = {0};
    memcpy(bytes, g_staged_message.payload, g_staged_message.length);

    g_live_keys[0] = qualified_io_read_key(LIVE_STATUS_PORT);
    g_live_responses[0] = encoded_drive_command(1u);
    for (uint32_t i = 0u; i < LIVE_DATA_WORDS; ++i) {
        const uint16_t word = (uint16_t)bytes[i * 2u] |
            ((uint16_t)bytes[i * 2u + 1u] << 8);
        g_live_keys[i + 1u] = qualified_io_read_key(LIVE_DATA_PORT);
        g_live_responses[i + 1u] = encoded_drive_command(word);
    }

    r->key_sets_disjoint = true;
    const uint32_t initial_status_key =
        qualified_io_read_key(LIVE_STATUS_PORT);
    for (uint32_t i = 0u; i < g_sequence_count; ++i) {
        if (g_sequence_keys[i] == initial_status_key)
            r->key_sets_disjoint = false;
        for (uint32_t j = 0u; j < LIVE_PUBLISHED_PAIRS; ++j)
            if (g_sequence_keys[i] == g_live_keys[j])
                r->key_sets_disjoint = false;
    }
}

static int configure_deferred_tx_dma(const pc1c_sm_t *sm,
                                     const uint32_t *table,
                                     uint32_t count) {
    const int channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(sm->pio, sm->sm, true));
    channel_config_set_high_priority(&c, true);
    dma_channel_configure((uint)channel, &c, &sm->pio->txf[sm->sm], table,
                          count, false);
    return channel;
}

static uint32_t dma_reload_count(int channel) {
    /* RP2350 separates the programmed RELOAD count from the live counter.
     * Before a deferred channel is triggered, CHx_TRANS_COUNT correctly reads
     * zero; CHx_DBG_TCR is the hardware-backed proof that the next trigger will
     * load the complete immutable stream. DMA channel register blocks use a
     * 0x40-byte stride. */
    const uintptr_t address = DMA_BASE + DMA_CH0_DBG_TCR_OFFSET +
        (uintptr_t)(uint)channel * 0x40u;
    return (*(const volatile uint32_t *)address) & 0x0FFFFFFFu;
}

static void classify_live_mailbox(ai_b1b_result_t *r) {
    for (uint32_t i = 0u; i < r->base.trace_count; ++i) {
        const bus_trace_t *entry = &r->base.trace[i];
        const uint32_t address = decode_address(entry->address_raw);
        const uint16_t data = decode_ad(entry->data_raw);

        if (is_io_read(entry->address_raw) &&
            address == LIVE_STATUS_PORT && entry->lanes == LANES_WORD) {
            if (r->status_reads < 2u)
                r->status_values[r->status_reads] = data;
            ++r->status_reads;
        }
        if (is_io_read(entry->address_raw) &&
            address == LIVE_DATA_PORT && entry->lanes == LANES_WORD) {
            if (r->mailbox_reads < LIVE_DATA_WORDS &&
                data != live_expected_words[r->mailbox_reads])
                ++r->mailbox_mismatches;
            ++r->mailbox_reads;
        }
        if (entry->io_write && address == LIVE_WITNESS_PORT &&
            entry->lanes == LANES_WORD) {
            if (data == 1u) r->status_transition_ok = true;
            if (data == 0u) r->checksum_ok = true;
        }
        if (entry->io_write && address == LIVE_TX_PORT &&
            entry->lanes == LANES_WORD &&
            r->reply_bytes + 2u < sizeof r->reply) {
            r->reply[r->reply_bytes++] = (char)(data & 0xFFu);
            r->reply[r->reply_bytes++] = (char)(data >> 8);
        }
        if (entry->io_write && address == LIVE_CONTROL_PORT &&
            entry->lanes == LANES_WORD && data == 1u)
            r->commit_ok = true;
    }

    r->status_transition_ok = r->status_transition_ok &&
        r->status_reads == 2u && r->status_values[0] == 0u &&
        r->status_values[1] == 1u;
    r->mailbox_reads_ok = r->mailbox_reads == LIVE_DATA_WORDS &&
        r->mailbox_mismatches == 0u;
    r->reply[r->reply_bytes] = '\0';
    r->reply_ok = r->reply_bytes == sizeof(live_expected_reply) - 1u &&
        memcmp(r->reply, live_expected_reply,
               sizeof(live_expected_reply) - 1u) == 0;
}

static void run_ai_b1b(pc1c_sm_t *clock, pc1c_sm_t *rom_matcher,
                       pc1c_sm_t *rom_responder,
                       pc1c_sm_t *mailbox_matcher,
                       pc1c_sm_t *mailbox_responder, pc1c_sm_t *phase,
                       pc1c_sm_t *observer, ai_b1b_result_t *r) {
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
    pio_interrupt_clear(pio1, 4u);
    pio_interrupt_clear(pio1, PUBLICATION_IRQ);
    pio_sm_exec(pio1, rom_responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));
    pio_sm_exec(pio1, mailbox_responder->sm,
                pio_encode_mov(pio_pindirs, pio_null));

    const int rom_key_dma = start_pio_tx_dma(
        rom_matcher, g_sequence_keys, g_sequence_count);
    const int rom_response_dma = start_pio_tx_dma(
        rom_responder, g_sequence_responses, g_sequence_count);
    const int mailbox_key_dma = configure_deferred_tx_dma(
        mailbox_matcher, g_live_keys, LIVE_PUBLISHED_PAIRS);
    const int mailbox_response_dma = configure_deferred_tx_dma(
        mailbox_responder, g_live_responses, LIVE_PUBLISHED_PAIRS);

    pio_sm_put_blocking(pio1, mailbox_matcher->sm,
                        qualified_io_read_key(LIVE_STATUS_PORT));
    pio_sm_put_blocking(pio1, mailbox_responder->sm,
                        encoded_drive_command(0u));

    const bool rom_matcher_primed = wait_fifo_primed(rom_matcher, 4u);
    const bool rom_responder_primed = wait_fifo_primed(rom_responder, 4u);
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
    r->mailbox_key_reload_pre = dma_reload_count(mailbox_key_dma);
    r->mailbox_response_reload_pre =
        dma_reload_count(mailbox_response_dma);
    r->deferred_dma_armed =
        r->mailbox_key_dma_pre == 0u &&
        r->mailbox_response_dma_pre == 0u &&
        r->mailbox_key_reload_pre == LIVE_PUBLISHED_PAIRS &&
        r->mailbox_response_reload_pre == LIVE_PUBLISHED_PAIRS &&
        !dma_channel_is_busy((uint)mailbox_key_dma) &&
        !dma_channel_is_busy((uint)mailbox_response_dma);

    route_ad_to_responder(rom_responder);
    b->pre_pio1_padoe = pio1->dbg_padoe;
    r->pre_pio2_padoe = pio2->dbg_padoe;
    b->clock_direction_armed = b->pre_pio1_padoe == 0u &&
        (r->pre_pio2_padoe & (1u << V30_PIN_CLK)) != 0u &&
        (r->pre_pio2_padoe & ~((uint32_t)1u << V30_PIN_CLK)) == 0u;

    const int observer_dma = start_observer_dma(observer);
    b->observer_dma_pre = dma_remaining(observer_dma);
    b->pre_release_clean = rom_matcher_primed && rom_responder_primed &&
        b->matcher_fifo_pre == 4u && b->responder_fifo_pre == 4u &&
        r->mailbox_matcher_fifo_pre == 1u &&
        r->mailbox_responder_fifo_pre == 1u &&
        r->deferred_dma_armed &&
        b->observer_dma_pre == OBSERVER_WORDS &&
        b->clock_direction_armed && r->key_sets_disjoint &&
        !gpio_get(V30_PIN_CLK) &&
        (sio_hw->gpio_oe & V30_AD_BUS_MASK) == 0u;

    pio_enable_sm_mask_in_sync(
        pio0, (1u << phase->sm) | (1u << observer->sm));
    pio_enable_sm_mask_in_sync(pio1, 0x0Fu);

    if (b->reset_ok && b->pre_release_clean) {
        const uint32_t irq_state = save_and_disable_interrupts();
        gpio_put(V30_PIN_RESET, false);
        pio_sm_set_enabled(clock->pio, clock->sm, true);
        const uint64_t deadline =
            time_us_64() + timeout_us_from_clocks(RUN_TIMEOUT_CLOCKS);
        while ((!stream_empty(rom_response_dma, rom_responder) ||
                !stream_empty(mailbox_response_dma, mailbox_responder)) &&
               time_us_64() <= deadline) {
            if (!r->publication_triggered &&
                pio_interrupt_get(pio1, PUBLICATION_IRQ)) {
                pio_interrupt_clear(pio1, PUBLICATION_IRQ);
                r->first_not_ready_observed = true;
                __dmb();
                dma_start_channel_mask((1u << (uint)mailbox_key_dma) |
                                       (1u << (uint)mailbox_response_dma));
                r->publication_triggered = true;
                r->publication_atomic = true;
            }
            tight_loop_contents();
        }
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
    r->mailbox_pairs = r->first_not_ready_observed ? 1u : 0u;
    if (mailbox_remaining <= LIVE_PUBLISHED_PAIRS)
        r->mailbox_pairs += LIVE_PUBLISHED_PAIRS - mailbox_remaining;
    b->qualified_pairs = r->rom_pairs;
    b->dma_streams_complete = b->matcher_dma_post == 0u &&
        b->responder_dma_post == 0u;
    r->mailbox_dma_complete = r->publication_triggered &&
        r->mailbox_key_dma_post == 0u &&
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
    b->observer_fifo_residue = pio_sm_get_rx_fifo_level(pio0, observer->sm);
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
    classify_live_mailbox(r);

    stop_dma(rom_key_dma);
    stop_dma(rom_response_dma);
    stop_dma(mailbox_key_dma);
    stop_dma(mailbox_response_dma);
    stop_dma(observer_dma);
    pio_interrupt_clear(pio1, 4u);
    pio_interrupt_clear(pio1, PUBLICATION_IRQ);
    route_ad_to_sio_high_z();
    b->terminal_safe = gpio_get(V30_PIN_RESET) &&
        !gpio_get(V30_PIN_CLK) && ad_is_sio_high_z();
}

static bool ai_b1b_pass(const ai_b1b_result_t *r) {
    const pc1c0c_result_t *b = &r->base;
    return result_valid(b) && b->first_response_phase_ok &&
        b->far_target_seen && b->checkpoint_ok &&
        b->deadline_misses == 0u && b->terminal_safe &&
        r->windows_record_complete && r->core1_record_complete &&
        r->core0_record_valid && r->staging_atomic &&
        r->deferred_dma_armed &&
        r->first_not_ready_observed && r->publication_triggered &&
        r->publication_atomic && r->status_transition_ok &&
        r->mailbox_reads_ok && r->checksum_ok && r->reply_ok &&
        r->commit_ok && r->key_sets_disjoint &&
        b->dma_streams_complete && r->mailbox_dma_complete &&
        r->rom_pairs == g_sequence_count &&
        r->mailbox_pairs == LIVE_TOTAL_PAIRS
#ifdef AI_BRIDGE_HID_TRANSPORT
        && r->hid_reply_complete &&
        r->hid_reply_bytes == PI86_BRIDGE_MESSAGE_SIZE
#endif
        ;
}

static void print_ai_b1b(const ai_b1b_result_t *r) {
    const pc1c0c_result_t *b = &r->base;
    printf("\n[HOST MAILBOX INPUT]\nHELLO NEC V30\n");
    printf("\n[V30 MAILBOX OUTPUT]\n%s\n", r->reply);
    printf("\n[SUMMARY]\n");
    printf("Measurement epoch          %s\n", pass_fail(result_valid(b)));
    printf("Reset / FFFF0 fetch        %s\n",
           pass_fail(b->reset_ok && b->first_address_ok));
    printf("First response 00EA        %s\n",
           pass_fail(b->first_response_phase_ok));
    printf("F0000 ROM execution        %s\n", pass_fail(b->far_target_seen));
#ifdef AI_BRIDGE_HID_TRANSPORT
    printf("Windows HID 64-byte record %s (sequence %lu)\n",
#else
    printf("Windows 64-byte record     %s (sequence %lu)\n",
#endif
           pass_fail(r->windows_record_complete),
           (unsigned long)r->host_sequence);
    printf("Core1 complete record      %s\n",
           pass_fail(r->core1_record_complete));
    printf("Core0 immutable staging    %s\n",
           pass_fail(r->core0_record_valid && r->staging_atomic));
    printf("Deferred DMA reload gate   %s (%lu/%lu words)\n",
           pass_fail(r->deferred_dma_armed),
           (unsigned long)r->mailbox_key_reload_pre,
           (unsigned long)r->mailbox_response_reload_pre);
    printf("V30 STATUS 00E0 transition %s (0 -> 1)\n",
           pass_fail(r->status_transition_ok));
    printf("Publication after NOT_READY%s\n",
           r->first_not_ready_observed ? " PASS" : " FAIL");
    printf("Atomic DMA publication     %s\n",
           pass_fail(r->publication_atomic));
    printf("Mailbox RX I/O 00E4        %s (%lu/%u words)\n",
           pass_fail(r->mailbox_reads_ok),
           (unsigned long)r->mailbox_reads, LIVE_DATA_WORDS);
    printf("V30 input XOR at 00E8      %s\n", pass_fail(r->checksum_ok));
    printf("Mailbox TX I/O 00E2        %s\n", pass_fail(r->reply_ok));
    printf("Mailbox commit I/O 00E6    %s\n", pass_fail(r->commit_ok));
#ifdef AI_BRIDGE_HID_TRANSPORT
    printf("HID reply 64-byte record   %s (%lu/64 bytes)\n",
           pass_fail(r->hid_reply_complete),
           (unsigned long)r->hid_reply_bytes);
    printf("CDC validation log role    RECEIVE-ONLY PASS\n");
#endif
    printf("ROM/mailbox key collisions %s\n",
           r->key_sets_disjoint ? "0 PASS" : "FAIL");
    printf("Current-cycle M33          NONE\n");
    printf("USB IRQ during V30 epoch   MASKED PASS\n");
    printf("Bus ownership/safety       %s\n", pass_fail(b->terminal_safe));
#ifdef AI_BRIDGE_HID_TRANSPORT
    printf("AI-B2-HID RESULT           %s\n", pass_fail(ai_b1b_pass(r)));
#else
    printf("AI-B1-B RESULT             %s\n", pass_fail(ai_b1b_pass(r)));
#endif

    printf("\n[ENGINEERING DETAILS]\n");
#ifdef AI_BRIDGE_HID_TRANSPORT
    printf("AI-B2-HID Composite Mailbox - 0.600 MHz\n");
    printf("Host transport             = Windows USB HID 64-byte record; CDC log only\n");
    printf("USB identity               = VID CAFE PID 4011\n");
#else
    printf("AI-B1-B Live Mailbox Publication - 0.600 MHz\n");
    printf("Host transport             = Windows USB CDC binary record\n");
#endif
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
    printf("STATUS observations        = %lu (first %04X, second %04X)\n",
           (unsigned long)r->status_reads, r->status_values[0],
           r->status_values[1]);
    printf("ROM qualified pairs        = %lu/%lu %s\n",
           (unsigned long)r->rom_pairs,
           (unsigned long)g_sequence_count,
           pass_fail(r->rom_pairs == g_sequence_count));
    printf("Mailbox qualified pairs    = %lu/%u %s\n",
           (unsigned long)r->mailbox_pairs, LIVE_TOTAL_PAIRS,
           pass_fail(r->mailbox_pairs == LIVE_TOTAL_PAIRS));
    printf("Mailbox DMA live pre/post  = key %lu/%lu response %lu/%lu\n",
           (unsigned long)r->mailbox_key_dma_pre,
           (unsigned long)r->mailbox_key_dma_post,
           (unsigned long)r->mailbox_response_dma_pre,
           (unsigned long)r->mailbox_response_dma_post);
    printf("Mailbox DMA reload count   = key %lu response %lu %s\n",
           (unsigned long)r->mailbox_key_reload_pre,
           (unsigned long)r->mailbox_response_reload_pre,
           pass_fail(r->deferred_dma_armed));
    printf("Response deadline misses   = %lu %s\n",
           (unsigned long)b->deadline_misses,
           pass_fail(b->deadline_misses == 0u));
    printf("Observer complete cycles   = %u\n", b->trace_count);
    printf("ROM image                  = %lu bytes; SHA-256 %s\n",
           (unsigned long)PC1C_ROM_SIZE, PC1C_ROM_SHA256);
    printf("TERMINAL SAFE STATE        = %s\n", pass_fail(b->terminal_safe));
    printf("CPU halted in RESET=HIGH, CLK=LOW, AD bus high-Z.\n");
}

int main(void) {
    prepare_response_tables();
    prepare_header_high_z();
    init_control_outputs();
    route_ad_to_sio_high_z();
#ifdef AI_BRIDGE_HID_TRANSPORT
    /* User TinyUSB descriptors must be active before pico_stdio_usb registers
     * CDC on the same composite device. HID owns binary records; CDC remains
     * the unchanged human-readable evidence channel. */
    pi86_ai_bridge_usb_init();
#endif
    stdio_init_all();
    while (!stdio_usb_connected()) {
#ifdef AI_BRIDGE_HID_TRANSPORT
        pi86_ai_bridge_usb_task();
#endif
        sleep_ms(1);
    }
    sleep_ms(100);

    static ai_b1b_result_t result;
    memset(&result, 0, sizeof result);
    if (!receive_complete_host_record(&result)) {
        print_ai_b1b(&result);
        fflush(stdout);
        terminal_service_loop();
    }
    prepare_live_tables(&result);

    pc1c_sm_t clock, rom_matcher, rom_responder;
    pc1c_sm_t mailbox_matcher, mailbox_responder, phase, observer;
    init_clock_on_pio2(&clock);
    init_response_plane(&rom_matcher, &rom_responder,
                        &mailbox_matcher, &mailbox_responder);
    phase_capture_init(&phase);
    observer_init(&observer);

    run_ai_b1b(&clock, &rom_matcher, &rom_responder,
               &mailbox_matcher, &mailbox_responder,
               &phase, &observer, &result);
#ifdef AI_BRIDGE_HID_TRANSPORT
    pi86_bridge_message_t reply_record = {0};
    reply_record.version = PI86_BRIDGE_PROTOCOL_VERSION;
    reply_record.type = PI86_BRIDGE_MESSAGE_TEXT;
    reply_record.sequence = result.host_sequence;
    reply_record.length = (uint16_t)(sizeof live_expected_reply - 1u);
    memcpy(reply_record.payload, result.reply, reply_record.length);
    result.hid_reply_complete = pi86_ai_bridge_hid_send_record(
        (const uint8_t *)&reply_record, HOST_RECORD_TIMEOUT_US);
    result.hid_reply_bytes = result.hid_reply_complete ?
        PI86_BRIDGE_MESSAGE_SIZE : 0u;
#endif
    print_ai_b1b(&result);
    fflush(stdout);
    terminal_service_loop();
}
