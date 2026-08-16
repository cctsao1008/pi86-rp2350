add_executable(gate10_8259a
    gate10_8259a.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
)

target_include_directories(gate10_8259a PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate10_8259a
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate10_8259a
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate10_8259a 1)
pico_enable_stdio_uart(gate10_8259a 0)

pico_add_extra_outputs(gate10_8259a)
