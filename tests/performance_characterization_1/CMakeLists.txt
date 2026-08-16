add_executable(performance_characterization_1
    performance_characterization_1.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(performance_characterization_1 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(performance_characterization_1
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(performance_characterization_1
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(performance_characterization_1 1)
pico_enable_stdio_uart(performance_characterization_1 0)

pico_add_extra_outputs(performance_characterization_1)

add_executable(performance_characterization_1_polling_baseline
    performance_characterization_1_polling_baseline.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(performance_characterization_1_polling_baseline PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(performance_characterization_1_polling_baseline
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(performance_characterization_1_polling_baseline
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(performance_characterization_1_polling_baseline 1)
pico_enable_stdio_uart(performance_characterization_1_polling_baseline 0)

pico_add_extra_outputs(performance_characterization_1_polling_baseline)
