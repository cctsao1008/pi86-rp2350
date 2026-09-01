#ifndef RP86_CDC_COMMAND_PARSER_H
#define RP86_CDC_COMMAND_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#define RP86_CDC_COMMAND_CAPACITY 48u

typedef enum {
    RP86_CDC_COMMAND_STATUS = 0,
    RP86_CDC_COMMAND_ENTER_BOOTLOADER,
    RP86_CDC_COMMAND_REBOOT,
    RP86_CDC_COMMAND_INVALID,
} rp86_cdc_command_t;

typedef struct {
    char text[RP86_CDC_COMMAND_CAPACITY];
    size_t length;
    bool overflowed;
} rp86_cdc_command_parser_t;

void rp86_cdc_command_parser_init(rp86_cdc_command_parser_t *parser);

/* Returns true only when a complete command or invalid line is available. */
bool rp86_cdc_command_parser_feed(rp86_cdc_command_parser_t *parser,
                                  char character,
                                  rp86_cdc_command_t *command);

#endif
