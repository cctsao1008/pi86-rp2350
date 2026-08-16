add_executable(gate9_interrupt
    gate9_interrupt.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
)

target_include_directories(gate9_interrupt PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate9_interrupt
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate9_interrupt
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate9_interrupt 1)
pico_enable_stdio_uart(gate9_interrupt 0)

pico_add_extra_outputs(gate9_interrupt)
