add_executable(gate11_irq_priority
    gate11_irq_priority.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
)

target_include_directories(gate11_irq_priority PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate11_irq_priority
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate11_irq_priority
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate11_irq_priority 1)
pico_enable_stdio_uart(gate11_irq_priority 0)

pico_add_extra_outputs(gate11_irq_priority)
