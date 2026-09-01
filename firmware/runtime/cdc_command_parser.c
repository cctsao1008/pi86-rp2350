#include "runtime/cdc_command_parser.h"

#include <string.h>

void rp86_cdc_command_parser_init(rp86_cdc_command_parser_t *parser) {
    memset(parser, 0, sizeof *parser);
}

static rp86_cdc_command_t parse_command(const char *text) {
    if (strcmp(text, "RP86 STATUS") == 0 || strcmp(text, "status") == 0)
        return RP86_CDC_COMMAND_STATUS;
    if (strcmp(text, "RP86 BOOTLOADER") == 0 ||
        strcmp(text, "bootloader") == 0 || strcmp(text, "bootsel") == 0)
        return RP86_CDC_COMMAND_ENTER_BOOTLOADER;
    if (strcmp(text, "RP86 REBOOT") == 0 || strcmp(text, "reboot") == 0)
        return RP86_CDC_COMMAND_REBOOT;
    return RP86_CDC_COMMAND_INVALID;
}

bool rp86_cdc_command_parser_feed(rp86_cdc_command_parser_t *parser,
                                  char character,
                                  rp86_cdc_command_t *command) {
    if (character == '\r' || character == '\n') {
        if (parser->overflowed) {
            parser->length = 0u;
            parser->overflowed = false;
            *command = RP86_CDC_COMMAND_INVALID;
            return true;
        }
        if (parser->length == 0u) return false;

        parser->text[parser->length] = '\0';
        parser->length = 0u;
        *command = parse_command(parser->text);
        return true;
    }

    if (parser->overflowed) return false;
    if (parser->length + 1u < sizeof parser->text) {
        parser->text[parser->length++] = character;
    } else {
        parser->length = 0u;
        parser->overflowed = true;
    }
    return false;
}
