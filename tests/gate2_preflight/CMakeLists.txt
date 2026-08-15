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

add_executable(gate2_powered_static
    gate2_powered_static.c
)

target_include_directories(gate2_powered_static PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate2_powered_static
    pico_stdlib
)

pico_enable_stdio_usb(gate2_powered_static 1)
pico_enable_stdio_uart(gate2_powered_static 0)

pico_add_extra_outputs(gate2_powered_static)
