#ifndef RP86_RUNTIME_EVIDENCE_QUEUE_H
#define RP86_RUNTIME_EVIDENCE_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RP86_EVIDENCE_QUEUE_CAPACITY = 2048u };

typedef struct {
    uint8_t data[RP86_EVIDENCE_QUEUE_CAPACITY];
    size_t read_offset;
    size_t write_offset;
    size_t used;
    uint32_t producer_drops;
} rp86_evidence_queue_t;

void rp86_evidence_queue_reset(rp86_evidence_queue_t *queue);
bool rp86_evidence_queue_try_push(rp86_evidence_queue_t *queue,
                                  const void *data, size_t length);
size_t rp86_evidence_queue_peek(const rp86_evidence_queue_t *queue,
                                const uint8_t **data);
void rp86_evidence_queue_consume(rp86_evidence_queue_t *queue,
                                 size_t length);
uint32_t rp86_evidence_queue_drops(const rp86_evidence_queue_t *queue);

#endif
