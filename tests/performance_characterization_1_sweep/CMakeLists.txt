add_executable(pc1b_pio_dma_read_timing_sweep
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/pc1b_pio_dma_read_timing_sweep.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1b_pio_dma_read_timing_sweep PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1
)

target_link_libraries(pc1b_pio_dma_read_timing_sweep
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1b_pio_dma_read_timing_sweep
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1b_pio_dma_read_timing_sweep
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1b_pio_dma_read_timing_sweep
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/pc1b_dma_timing_sweep.pio
)

pico_enable_stdio_usb(pc1b_pio_dma_read_timing_sweep 1)
pico_enable_stdio_uart(pc1b_pio_dma_read_timing_sweep 0)

pico_add_extra_outputs(pc1b_pio_dma_read_timing_sweep)
