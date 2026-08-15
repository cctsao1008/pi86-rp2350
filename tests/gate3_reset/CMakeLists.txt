add_executable(gate3_ffff0
    gate3_ffff0.c
)

target_include_directories(gate3_ffff0 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate3_ffff0
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate3_ffff0
    ${CMAKE_CURRENT_LIST_DIR}/gate3_capture.pio
)

pico_enable_stdio_usb(gate3_ffff0 1)
pico_enable_stdio_uart(gate3_ffff0 0)

pico_add_extra_outputs(gate3_ffff0)
