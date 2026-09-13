add_executable(execution_clock_runtime
    execution_clock_runtime.c
    ${PROJECT_SOURCE_DIR}/firmware/runtime/execution_clock_controller.c
    ${PROJECT_SOURCE_DIR}/firmware/runtime/clock_stepped_bus_controller.c
    ${PROJECT_SOURCE_DIR}/firmware/bus/processor_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
)

target_include_directories(execution_clock_runtime PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(execution_clock_runtime
    execution_clock_transition_workload
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(execution_clock_runtime
    ${PROJECT_SOURCE_DIR}/firmware/runtime/clock_stepped_clock.pio
)
pico_generate_pio_header(execution_clock_runtime
    ${PROJECT_SOURCE_DIR}/firmware/runtime/execution_clock_control.pio
)

pico_enable_stdio_usb(execution_clock_runtime 1)
pico_enable_stdio_uart(execution_clock_runtime 0)
pico_add_extra_outputs(execution_clock_runtime)
