if(NOT EXISTS "${PI86_FIRMWARE_BIN}")
    message(FATAL_ERROR "Canonical firmware binary not found: ${PI86_FIRMWARE_BIN}")
endif()

file(SIZE "${PI86_FIRMWARE_BIN}" PI86_FIRMWARE_SIZE)
if(PI86_FIRMWARE_SIZE GREATER PI86_FIRMWARE_LIMIT)
    message(FATAL_ERROR
        "Canonical firmware is ${PI86_FIRMWARE_SIZE} bytes; the flash:/ layout "
        "reserves only ${PI86_FIRMWARE_LIMIT} bytes for firmware")
endif()

message(STATUS
    "Canonical firmware uses ${PI86_FIRMWARE_SIZE}/${PI86_FIRMWARE_LIMIT} bytes "
    "before flash:/")
