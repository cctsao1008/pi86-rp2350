#ifndef RP86_PREPARED_RESPONDER_H
#define RP86_PREPARED_RESPONDER_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

#define RP86_PREPARED_OUT_BASE 0u
#define RP86_PREPARED_OUT_COUNT 28u
#define RP86_PREPARED_RESPONSE_VALID_BIT 28u
#define RP86_PROCESSOR_RESET_CLOCKS 20u
#define RP86_PREPARED_OBSERVER_CYCLES 256u
#define RP86_PREPARED_OBSERVER_WORDS (RP86_PREPARED_OBSERVER_CYCLES * 2u)

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
} rp86_prepared_sm_t;

uint32_t rp86_prepared_sample_bit(uint32_t sample, uint gpio);
uint16_t rp86_prepared_decode_ad(uint32_t sample);
uint32_t rp86_prepared_decode_address(uint32_t sample);
bool rp86_prepared_is_memory_read(uint32_t sample);
bool rp86_prepared_is_memory_write(uint32_t sample);

uint32_t rp86_prepared_encode_word(uint16_t value);
uint32_t rp86_prepared_encode_address(uint32_t address);
uint32_t rp86_prepared_encode_drive(uint16_t value);
uint32_t rp86_prepared_memory_read_key(uint32_t address);

void rp86_prepared_header_high_z(void);
void rp86_prepared_control_outputs_init(void);
void rp86_prepared_route_ad_to_sio_high_z(void);
void rp86_prepared_route_ad_to_responder(const rp86_prepared_sm_t *responder);
bool rp86_prepared_non_ad_pins_isolated(
    const rp86_prepared_sm_t *responder);
bool rp86_prepared_wait_reset_clocks(uint count, uint32_t processor_hz);

void rp86_prepared_observer_init(rp86_prepared_sm_t *observer);
void rp86_prepared_sm_arm(rp86_prepared_sm_t *sm);
int rp86_prepared_start_tx_dma(const rp86_prepared_sm_t *sm,
                               const uint32_t *words, uint32_t count);
int rp86_prepared_start_observer_dma(const rp86_prepared_sm_t *observer,
                                     uint32_t *words, uint32_t count);
uint32_t rp86_prepared_dma_remaining(int channel);
bool rp86_prepared_wait_fifo_primed(const rp86_prepared_sm_t *sm,
                                    uint32_t level);

#endif
