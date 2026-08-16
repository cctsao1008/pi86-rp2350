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

add_executable(pc1a_rev1_300khz
    pc1a_rev1_300khz.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_300khz PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_300khz
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_300khz
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_300khz 1)
pico_enable_stdio_uart(pc1a_rev1_300khz 0)

pico_add_extra_outputs(pc1a_rev1_300khz)

add_executable(pc1a_rev1_boot_trace
    pc1a_rev1_boot_trace.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_boot_trace PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_boot_trace
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_boot_trace
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_boot_trace 1)
pico_enable_stdio_uart(pc1a_rev1_boot_trace 0)

pico_add_extra_outputs(pc1a_rev1_boot_trace)

add_executable(pc1a_rev1_boot_trace_release1
    pc1a_rev1_boot_trace_release1.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_boot_trace_release1 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_boot_trace_release1
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_boot_trace_release1
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_boot_trace_release1 1)
pico_enable_stdio_uart(pc1a_rev1_boot_trace_release1 0)

pico_add_extra_outputs(pc1a_rev1_boot_trace_release1)

add_executable(pc1a_rev1_boot_trace_fastdrive
    pc1a_rev1_boot_trace_fastdrive.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_boot_trace_fastdrive PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_boot_trace_fastdrive
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_boot_trace_fastdrive
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_boot_trace_fastdrive 1)
pico_enable_stdio_uart(pc1a_rev1_boot_trace_fastdrive 0)

pico_add_extra_outputs(pc1a_rev1_boot_trace_fastdrive)

add_executable(pc1a_rev1_free_run_timeline
    pc1a_rev1_free_run_timeline.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_free_run_timeline PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_free_run_timeline
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_free_run_timeline
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_free_run_timeline 1)
pico_enable_stdio_uart(pc1a_rev1_free_run_timeline 0)

pico_add_extra_outputs(pc1a_rev1_free_run_timeline)

add_executable(pc1a_rev1_free_run_timeline_600khz
    pc1a_rev1_free_run_timeline_600khz.c
)

target_include_directories(pc1a_rev1_free_run_timeline_600khz PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_free_run_timeline_600khz
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_free_run_timeline_600khz
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_free_run_timeline_600khz 1)
pico_enable_stdio_uart(pc1a_rev1_free_run_timeline_600khz 0)

pico_add_extra_outputs(pc1a_rev1_free_run_timeline_600khz)

add_executable(pc1a_rev1_read_timing_matrix
    pc1a_rev1_read_timing_matrix.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_read_timing_matrix PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_read_timing_matrix
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_read_timing_matrix
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)

pico_enable_stdio_usb(pc1a_rev1_read_timing_matrix 1)
pico_enable_stdio_uart(pc1a_rev1_read_timing_matrix 0)

pico_add_extra_outputs(pc1a_rev1_read_timing_matrix)

add_executable(pc1a_rev1_read_timing_matrix_v2
    pc1a_rev1_read_timing_matrix_v2.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_read_timing_matrix_v2 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_read_timing_matrix_v2
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_read_timing_matrix_v2
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1a_rev1_read_timing_matrix_v2
    ${CMAKE_CURRENT_LIST_DIR}/perf_ale_observer.pio
)

pico_enable_stdio_usb(pc1a_rev1_read_timing_matrix_v2 1)
pico_enable_stdio_uart(pc1a_rev1_read_timing_matrix_v2 0)

pico_add_extra_outputs(pc1a_rev1_read_timing_matrix_v2)

add_executable(pc1a_rev1_read_timing_matrix_v3
    pc1a_rev1_read_timing_matrix_v3.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_read_timing_matrix_v3 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_read_timing_matrix_v3
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_read_timing_matrix_v3
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1a_rev1_read_timing_matrix_v3
    ${CMAKE_CURRENT_LIST_DIR}/perf_ale_observer.pio
)

pico_enable_stdio_usb(pc1a_rev1_read_timing_matrix_v3 1)
pico_enable_stdio_uart(pc1a_rev1_read_timing_matrix_v3 0)

pico_add_extra_outputs(pc1a_rev1_read_timing_matrix_v3)

add_executable(pc1a_rev1_reset_vector_d2h2
    pc1a_rev1_reset_vector_d2h2.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_reset_vector_d2h2 PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_reset_vector_d2h2
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_reset_vector_d2h2
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1a_rev1_reset_vector_d2h2
    ${CMAKE_CURRENT_LIST_DIR}/perf_ale_observer.pio
)

pico_enable_stdio_usb(pc1a_rev1_reset_vector_d2h2 1)
pico_enable_stdio_uart(pc1a_rev1_reset_vector_d2h2 0)

pico_add_extra_outputs(pc1a_rev1_reset_vector_d2h2)

add_executable(pc1a_rev1_reset_vector_hot_pipeline
    pc1a_rev1_reset_vector_hot_pipeline.c
    ${PROJECT_SOURCE_DIR}/firmware/memory/memory.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
    ${PROJECT_SOURCE_DIR}/firmware/pit/pit.c
)

target_include_directories(pc1a_rev1_reset_vector_hot_pipeline PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1a_rev1_reset_vector_hot_pipeline
    pico_stdlib
    hardware_clocks
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1a_rev1_reset_vector_hot_pipeline
    ${CMAKE_CURRENT_LIST_DIR}/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1a_rev1_reset_vector_hot_pipeline
    ${CMAKE_CURRENT_LIST_DIR}/perf_ale_observer.pio
)

pico_enable_stdio_usb(pc1a_rev1_reset_vector_hot_pipeline 1)
pico_enable_stdio_uart(pc1a_rev1_reset_vector_hot_pipeline 0)

pico_add_extra_outputs(pc1a_rev1_reset_vector_hot_pipeline)
