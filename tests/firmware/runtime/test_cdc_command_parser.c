#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "runtime/cdc_command_parser.h"

static rp86_cdc_command_t feed_line(rp86_cdc_command_parser_t *parser,
                                    const char *line) {
    rp86_cdc_command_t command = RP86_CDC_COMMAND_INVALID;
    bool complete = false;
    for (size_t i = 0u; line[i] != '\0'; ++i)
        complete = rp86_cdc_command_parser_feed(parser, line[i], &command);
    assert(complete);
    return command;
}

int main(void) {
    rp86_cdc_command_parser_t parser;
    rp86_cdc_command_parser_init(&parser);

    assert(feed_line(&parser, "status\n") == RP86_CDC_COMMAND_STATUS);
    assert(feed_line(&parser, "RP86 STATUS\r") == RP86_CDC_COMMAND_STATUS);
    assert(feed_line(&parser, "bootsel\n") ==
           RP86_CDC_COMMAND_ENTER_BOOTLOADER);
    assert(feed_line(&parser, "reboot\n") == RP86_CDC_COMMAND_REBOOT);
    assert(feed_line(&parser, "unknown\n") == RP86_CDC_COMMAND_INVALID);

    char oversized[RP86_CDC_COMMAND_CAPACITY + 4u];
    memset(oversized, 'x', sizeof oversized);
    oversized[sizeof oversized - 2u] = '\n';
    oversized[sizeof oversized - 1u] = '\0';
    assert(feed_line(&parser, oversized) == RP86_CDC_COMMAND_INVALID);

    rp86_cdc_command_t command = RP86_CDC_COMMAND_INVALID;
    assert(!rp86_cdc_command_parser_feed(&parser, '\n', &command));
    return 0;
}
