#include "usb_transport.h"

#include <string.h>

#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "runtime/spsc_record_slot.h"
#include "tusb.h"

/* USB stack callbacks execute on Core0 through the Pico SDK stdio USB worker.
 * The complete OUT report is published only after all 64 bytes are copied.
 * Core1 then takes the immutable record and transfers it through the existing
 * SPSC ownership boundary. The slot can be reused after a complete take; no
 * partial HID report is ever mailbox-visible and a pending record is never
 * overwritten. */
static rp86_spsc_record_slot_t g_hid_out_slot;
static volatile bool g_hid_in_complete;

void rp86_host_protocol_usb_init(void) {
    const tusb_rhport_init_t device = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    hard_assert(tusb_init(BOARD_TUD_RHPORT, &device));
}

void rp86_host_protocol_usb_task(void) {
    /* Linking tinyusb_device deliberately disables pico_stdio_usb's
     * background task. Core0 owns every explicit tud_task() call; callers do
     * not invoke this function during the deterministic V30 epoch. */
    hard_assert(get_core_num() == 0u);
    tud_task();
}

bool rp86_host_protocol_cdc_write(const char *data, uint32_t length,
                              uint32_t timeout_us) {
    const uint64_t deadline = time_us_64() + timeout_us;
    uint32_t offset = 0u;
    while (offset < length && time_us_64() <= deadline) {
        rp86_host_protocol_usb_task();
        if (!tud_cdc_connected()) continue;
        const uint32_t available = tud_cdc_write_available();
        if (available == 0u) {
            tud_cdc_write_flush();
            continue;
        }
        uint32_t count = length - offset;
        if (count > available) count = available;
        offset += tud_cdc_write(data + offset, count);
        tud_cdc_write_flush();
    }
    rp86_host_protocol_usb_task();
    return offset == length;
}

uint32_t rp86_host_protocol_cdc_try_write(const char *data, uint32_t length) {
    if (data == NULL || length == 0u || !tud_cdc_connected()) return 0u;
    const uint32_t available = tud_cdc_write_available();
    if (available == 0u) return 0u;
    if (length > available) length = available;
    const uint32_t written = tud_cdc_write(data, length);
    if (written != 0u) tud_cdc_write_flush();
    return written;
}

bool rp86_host_protocol_hid_take_record(
    uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE]) {
    return rp86_spsc_record_try_take(&g_hid_out_slot, record);
}

bool rp86_host_protocol_hid_send_record(
    const uint8_t record[RP86_HOST_PROTOCOL_MESSAGE_SIZE], uint32_t timeout_us) {
    const uint64_t deadline = time_us_64() + timeout_us;
    g_hid_in_complete = false;
    while (!tud_hid_ready() && time_us_64() <= deadline)
        rp86_host_protocol_usb_task();
    if (!tud_hid_ready()) return false;

    /* The background TinyUSB worker runs on this same core. Prevent it from
     * entering tud_task() while the fixed report is queued. */
    const uint32_t irq_state = save_and_disable_interrupts();
    const bool queued = tud_hid_report(0u, record, RP86_HOST_PROTOCOL_MESSAGE_SIZE);
    restore_interrupts(irq_state);
    if (!queued) return false;

    while (!g_hid_in_complete && time_us_64() <= deadline)
        rp86_host_protocol_usb_task();
    return g_hid_in_complete;
}

uint32_t rp86_host_protocol_hid_producer_drops(void) {
    return rp86_spsc_record_drops(&g_hid_out_slot);
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
    if (size != RP86_HOST_PROTOCOL_MESSAGE_SIZE) return;
    (void)rp86_spsc_record_try_publish(&g_hid_out_slot, buffer);
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report,
                                uint16_t length) {
    (void)instance;
    (void)report;
    if (length == RP86_HOST_PROTOCOL_MESSAGE_SIZE) g_hid_in_complete = true;
}
