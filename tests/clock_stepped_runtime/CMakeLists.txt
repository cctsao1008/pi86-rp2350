include("${PROJECT_SOURCE_DIR}/cmake/ProcessorImage.cmake")

rp86_add_processor_image(clock_stepped_general_workload
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/workloads/clock_stepped_general.asm"
    SYMBOL clock_stepped_general_workload
)

add_executable(clock_stepped_native_runtime
    clock_stepped_native_runtime.c
    ${PROJECT_SOURCE_DIR}/firmware/runtime/clock_stepped_bus_controller.c
    ${PROJECT_SOURCE_DIR}/firmware/processor/processor_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
)

target_include_directories(clock_stepped_native_runtime PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(clock_stepped_native_runtime
    clock_stepped_general_workload
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(clock_stepped_native_runtime
    ${PROJECT_SOURCE_DIR}/firmware/runtime/clock_stepped_clock.pio
)

pico_enable_stdio_usb(clock_stepped_native_runtime 1)
pico_enable_stdio_uart(clock_stepped_native_runtime 0)
pico_add_extra_outputs(clock_stepped_native_runtime)
