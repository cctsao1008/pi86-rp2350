add_executable(gate12_pit_irq0
    gate12_pit_irq0.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(gate12_pit_irq0 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate12_pit_irq0
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate12_pit_irq0
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate12_pit_irq0 1)
pico_enable_stdio_uart(gate12_pit_irq0 0)

pico_add_extra_outputs(gate12_pit_irq0)
