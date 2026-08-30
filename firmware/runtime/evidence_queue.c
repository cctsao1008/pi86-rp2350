#include "runtime/evidence_queue.h"

#include <string.h>

void rp86_evidence_queue_reset(rp86_evidence_queue_t *queue) {
    if (queue != NULL) memset(queue, 0, sizeof *queue);
}

bool rp86_evidence_queue_try_push(rp86_evidence_queue_t *queue,
                                  const void *data, size_t length) {
    if (queue == NULL || (length != 0u && data == NULL)) return false;
    if (length == 0u) return true;
    if (length > RP86_EVIDENCE_QUEUE_CAPACITY - queue->used) {
        ++queue->producer_drops;
        return false;
    }

    const size_t first = length < RP86_EVIDENCE_QUEUE_CAPACITY -
                                      queue->write_offset ?
                             length : RP86_EVIDENCE_QUEUE_CAPACITY -
                                          queue->write_offset;
    memcpy(queue->data + queue->write_offset, data, first);
    memcpy(queue->data, (const uint8_t *)data + first, length - first);
    queue->write_offset =
        (queue->write_offset + length) % RP86_EVIDENCE_QUEUE_CAPACITY;
    queue->used += length;
    return true;
}

size_t rp86_evidence_queue_peek(const rp86_evidence_queue_t *queue,
                                const uint8_t **data) {
    if (queue == NULL || data == NULL || queue->used == 0u) return 0u;
    *data = queue->data + queue->read_offset;
    const size_t contiguous = RP86_EVIDENCE_QUEUE_CAPACITY -
                              queue->read_offset;
    return queue->used < contiguous ? queue->used : contiguous;
}

void rp86_evidence_queue_consume(rp86_evidence_queue_t *queue,
                                 size_t length) {
    if (queue == NULL) return;
    if (length > queue->used) length = queue->used;
    queue->read_offset =
        (queue->read_offset + length) % RP86_EVIDENCE_QUEUE_CAPACITY;
    queue->used -= length;
}

uint32_t rp86_evidence_queue_drops(const rp86_evidence_queue_t *queue) {
    return queue == NULL ? 0u : queue->producer_drops;
}
