add_executable(gate9r_pic
    gate9r_pic.c
    ${PROJECT_SOURCE_DIR}/firmware/v30/v30_bus.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
)

target_include_directories(gate9r_pic PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate9r_pic
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(gate9r_pic
    ${PROJECT_SOURCE_DIR}/tests/gate4_memread/gate4_step_clock.pio
)

pico_enable_stdio_usb(gate9r_pic 1)
pico_enable_stdio_uart(gate9r_pic 0)

pico_add_extra_outputs(gate9r_pic)
