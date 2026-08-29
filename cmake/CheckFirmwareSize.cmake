if(NOT EXISTS "${RP86_FIRMWARE_BIN}")
    message(FATAL_ERROR "Canonical firmware binary not found: ${RP86_FIRMWARE_BIN}")
endif()

file(SIZE "${RP86_FIRMWARE_BIN}" RP86_FIRMWARE_SIZE)
if(RP86_FIRMWARE_SIZE GREATER RP86_FIRMWARE_LIMIT)
    message(FATAL_ERROR
        "Canonical firmware is ${RP86_FIRMWARE_SIZE} bytes; the flash:/ layout "
        "reserves only ${RP86_FIRMWARE_LIMIT} bytes for firmware")
endif()

message(STATUS
    "Canonical firmware uses ${RP86_FIRMWARE_SIZE}/${RP86_FIRMWARE_LIMIT} bytes "
    "before flash:/")
