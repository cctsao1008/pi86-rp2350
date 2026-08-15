add_executable(gate2_safe
    gate2_safe.c
)

target_include_directories(gate2_safe PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate2_safe
    pico_stdlib
)

pico_enable_stdio_usb(gate2_safe 1)
pico_enable_stdio_uart(gate2_safe 0)

pico_add_extra_outputs(gate2_safe)
