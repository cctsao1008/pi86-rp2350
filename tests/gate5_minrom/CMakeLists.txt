add_executable(gate5_minrom
    gate5_minrom.c
)

target_include_directories(gate5_minrom PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate5_minrom
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate5_minrom
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate5_minrom 1)
pico_enable_stdio_uart(gate5_minrom 0)

pico_add_extra_outputs(gate5_minrom)
