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

function(add_pc1b_pio_direct_target target_name v30_hz)
    add_executable(${target_name}
        pc1b_pio_direct_post_reset_epoch.c
        ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
        ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
        ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
    )

    target_compile_definitions(${target_name} PRIVATE
        PC1B_DIRECT_V30_HZ=${v30_hz}u
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
        hardware_sync
    )

    pico_generate_pio_header(${target_name}
        ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
    )
    pico_generate_pio_header(${target_name}
        ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
    )
    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/pc1b_pio_direct_ad.pio
    )
    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)
    pico_add_extra_outputs(${target_name})
endfunction()

add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch          300000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_600khz   600000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_1200khz 1200000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_2mhz    2000000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_3mhz    3000000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_4mhz    4000000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_5mhz    5000000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_6mhz    6000000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_7mhz    7000000)
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_8mhz    8000000)
