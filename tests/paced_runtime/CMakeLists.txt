include("${PROJECT_SOURCE_DIR}/cmake/V30Rom.cmake")

pi86_add_v30_rom(paced_general_workload
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/workloads/paced_general.asm"
    SYMBOL paced_general_workload
)

add_executable(paced_native_runtime
    paced_native_runtime.c
    ${PROJECT_SOURCE_DIR}/firmware/runtime/paced_bus_engine.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
)

target_include_directories(paced_native_runtime PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(paced_native_runtime
    paced_general_workload
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(paced_native_runtime
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(paced_native_runtime 1)
pico_enable_stdio_uart(paced_native_runtime 0)
pico_add_extra_outputs(paced_native_runtime)
