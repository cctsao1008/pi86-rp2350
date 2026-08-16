add_executable(pc1b_pio_dma_extended_control_flow
    pc1b_pio_dma_extended_control_flow.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1b_pio_dma_extended_control_flow PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1b_pio_dma_extended_control_flow
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1b_pio_dma_extended_control_flow
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1b_pio_dma_extended_control_flow
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1b_pio_dma_extended_control_flow
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/pc1b_dma_ordered_events.pio
)

pico_enable_stdio_usb(pc1b_pio_dma_extended_control_flow 1)
pico_enable_stdio_uart(pc1b_pio_dma_extended_control_flow 0)

pico_add_extra_outputs(pc1b_pio_dma_extended_control_flow)

add_executable(pc1b_pio_dma_af_window
    pc1b_pio_dma_af_window.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1b_pio_dma_af_window PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1b_pio_dma_af_window
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1b_pio_dma_af_window
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1b_pio_dma_af_window
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1b_pio_dma_af_window
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_af_events.pio
)
pico_generate_pio_header(pc1b_pio_dma_af_window
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1b_pio_dma_af_window 1)
pico_enable_stdio_uart(pc1b_pio_dma_af_window 0)

pico_add_extra_outputs(pc1b_pio_dma_af_window)

add_executable(pc1b_pio_dma_post_reset_epoch
    pc1b_pio_dma_post_reset_epoch.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1b_pio_dma_post_reset_epoch PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1b_pio_dma_post_reset_epoch
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1b_pio_dma_post_reset_epoch
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1b_pio_dma_post_reset_epoch
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1b_pio_dma_post_reset_epoch
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_af_events.pio
)
pico_generate_pio_header(pc1b_pio_dma_post_reset_epoch
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1b_pio_dma_post_reset_epoch 1)
pico_enable_stdio_uart(pc1b_pio_dma_post_reset_epoch 0)

pico_add_extra_outputs(pc1b_pio_dma_post_reset_epoch)

add_executable(pc1b_pio_direct_post_reset_epoch
    pc1b_pio_direct_post_reset_epoch.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1b_pio_direct_post_reset_epoch PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1b_pio_direct_post_reset_epoch
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_pio_direct_ad.pio
)
pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1b_pio_direct_post_reset_epoch 1)
pico_enable_stdio_uart(pc1b_pio_direct_post_reset_epoch 0)

pico_add_extra_outputs(pc1b_pio_direct_post_reset_epoch)

add_executable(pc1b_pio_direct_post_reset_epoch_600khz
    pc1b_pio_direct_post_reset_epoch.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_compile_definitions(pc1b_pio_direct_post_reset_epoch_600khz PRIVATE
    PC1B_DIRECT_V30_HZ=600000u
)

target_include_directories(pc1b_pio_direct_post_reset_epoch_600khz PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1b_pio_direct_post_reset_epoch_600khz
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch_600khz
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch_600khz
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch_600khz
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_pio_direct_ad.pio
)
pico_generate_pio_header(pc1b_pio_direct_post_reset_epoch_600khz
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1b_pio_direct_post_reset_epoch_600khz 1)
pico_enable_stdio_uart(pc1b_pio_direct_post_reset_epoch_600khz 0)

pico_add_extra_outputs(pc1b_pio_direct_post_reset_epoch_600khz)
