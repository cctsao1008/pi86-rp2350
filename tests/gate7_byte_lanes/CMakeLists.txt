add_executable(gate7_byte_lanes
    gate7_byte_lanes.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
)

target_include_directories(gate7_byte_lanes PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate7_byte_lanes
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate7_byte_lanes
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate7_byte_lanes 1)
pico_enable_stdio_uart(gate7_byte_lanes 0)

pico_add_extra_outputs(gate7_byte_lanes)
