#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "runtime/evidence_queue.h"

int main(void) {
    rp86_evidence_queue_t queue;
    rp86_evidence_queue_reset(&queue);
    const uint8_t *view = NULL;
    assert(rp86_evidence_queue_peek(&queue, &view) == 0u);

    const uint8_t first[] = {1u, 2u, 3u, 4u};
    assert(rp86_evidence_queue_try_push(&queue, first, sizeof first));
    assert(rp86_evidence_queue_peek(&queue, &view) == sizeof first);
    assert(memcmp(view, first, sizeof first) == 0);
    rp86_evidence_queue_consume(&queue, 3u);

    uint8_t wrapping[RP86_EVIDENCE_QUEUE_CAPACITY - 2u];
    memset(wrapping, 0xA5, sizeof wrapping);
    assert(rp86_evidence_queue_try_push(&queue, wrapping, sizeof wrapping));
    assert(!rp86_evidence_queue_try_push(&queue, first, sizeof first));
    assert(rp86_evidence_queue_drops(&queue) == 1u);

    size_t consumed = 0u;
    while ((consumed = rp86_evidence_queue_peek(&queue, &view)) != 0u)
        rp86_evidence_queue_consume(&queue, consumed);
    assert(rp86_evidence_queue_try_push(&queue, first, sizeof first));
    assert(rp86_evidence_queue_peek(&queue, &view) == sizeof first);
    assert(memcmp(view, first, sizeof first) == 0);
    return 0;
}
