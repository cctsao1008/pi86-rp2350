#include "ai_bridge_usb.h"

#include <string.h>

#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "tusb.h"

/* USB stack callbacks execute on Core0 through the Pico SDK stdio USB worker.
 * The complete OUT report is published only after all 64 bytes are copied.
 * Core1 then takes the immutable record and transfers it through the existing
 * SPSC ownership boundary; no partial HID report is ever mailbox-visible. */
static uint8_t g_hid_out_record[PI86_BRIDGE_MESSAGE_SIZE];
static volatile bool g_hid_out_ready;
static volatile bool g_hid_out_taken;
static volatile bool g_hid_in_complete;

void pi86_ai_bridge_usb_init(void) {
    const tusb_rhport_init_t device = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    hard_assert(tusb_init(BOARD_TUD_RHPORT, &device));
}

void pi86_ai_bridge_usb_task(void) {
    /* Linking tinyusb_device deliberately disables pico_stdio_usb's
     * background task. Core0 owns every explicit tud_task() call; callers do
     * not invoke this function during the deterministic V30 epoch. */
    hard_assert(get_core_num() == 0u);
    tud_task();
}

bool pi86_ai_bridge_hid_take_record(
    uint8_t record[PI86_BRIDGE_MESSAGE_SIZE]) {
    if (!g_hid_out_ready || g_hid_out_taken) return false;
    __dmb();
    memcpy(record, g_hid_out_record, PI86_BRIDGE_MESSAGE_SIZE);
    __dmb();
    g_hid_out_taken = true;
    return true;
}

bool pi86_ai_bridge_hid_send_record(
    const uint8_t record[PI86_BRIDGE_MESSAGE_SIZE], uint32_t timeout_us) {
    const uint64_t deadline = time_us_64() + timeout_us;
    g_hid_in_complete = false;
    while (!tud_hid_ready() && time_us_64() <= deadline)
        pi86_ai_bridge_usb_task();
    if (!tud_hid_ready()) return false;

    /* The background TinyUSB worker runs on this same core. Prevent it from
     * entering tud_task() while the fixed report is queued. */
    const uint32_t irq_state = save_and_disable_interrupts();
    const bool queued = tud_hid_report(0u, record, PI86_BRIDGE_MESSAGE_SIZE);
    restore_interrupts(irq_state);
    if (!queued) return false;

    while (!g_hid_in_complete && time_us_64() <= deadline)
        pi86_ai_bridge_usb_task();
    return g_hid_in_complete;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t requested_length) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0u;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t size) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    if (size != PI86_BRIDGE_MESSAGE_SIZE || g_hid_out_ready) return;
    memcpy(g_hid_out_record, buffer, PI86_BRIDGE_MESSAGE_SIZE);
    __dmb();
    g_hid_out_ready = true;
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report,
                                uint16_t length) {
    (void)instance;
    (void)report;
    if (length == PI86_BRIDGE_MESSAGE_SIZE) g_hid_in_complete = true;
}
