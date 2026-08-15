function(add_gate4_memread_target target_name smoke_mode)
    add_executable(${target_name}
        gate4_memread.c
    )

    target_compile_definitions(${target_name} PRIVATE
        GATE4_SMOKE_MODE=${smoke_mode}
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_step_clock.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)

    pico_add_extra_outputs(${target_name})
endfunction()

function(add_gate4_matrix_target target_name source_file)
    add_executable(${target_name}
        ${source_file}
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_step_clock.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)

    pico_add_extra_outputs(${target_name})
endfunction()

function(add_gate4_diag_target target_name source_file)
    add_executable(${target_name}
        ${source_file}
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_step_clock.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)

    pico_add_extra_outputs(${target_name})
endfunction()

function(add_gate4_window_target target_name)
    add_executable(${target_name}
        gate4_read_window_capture.c
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_step_clock.pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_read_window_capture.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)

    pico_add_extra_outputs(${target_name})
endfunction()

function(add_gate4_dma_capture_target target_name)
    add_executable(${target_name}
        gate4_ale_reset_capture.c
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_step_clock.pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_ale_reset_capture.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)

    pico_add_extra_outputs(${target_name})
endfunction()

function(add_gate4_astb_edge_target target_name)
    add_executable(${target_name}
        gate4_astb_edge_verify.c
    )

    target_include_directories(${target_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware
    )

    target_link_libraries(${target_name}
        pico_stdlib
        hardware_clocks
        hardware_pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_step_clock.pio
    )

    pico_generate_pio_header(${target_name}
        ${CMAKE_CURRENT_LIST_DIR}/gate4_astb_edge_verify.pio
    )

    pico_enable_stdio_usb(${target_name} 1)
    pico_enable_stdio_uart(${target_name} 0)

    pico_add_extra_outputs(${target_name})
endfunction()

add_gate4_memread_target(gate4_memread 0)
add_gate4_memread_target(gate4_memread_smoke 1)
add_gate4_matrix_target(gate4_memread_matrix gate4_memread_matrix.c)
add_gate4_matrix_target(gate4_memread_matrix_v3 gate4_memread_matrix_v3.c)
add_gate4_diag_target(gate4_ad_line_diag gate4_ad_line_diag.c)
add_gate4_diag_target(gate4_ad_state_isolation gate4_ad_state_isolation.c)
add_gate4_diag_target(gate4_ad_phase_scan gate4_ad_phase_scan.c)
add_gate4_diag_target(gate4_pi86_reference gate4_pi86_reference.c)
add_gate4_diag_target(gate4_pi86_data_phase_diag gate4_pi86_data_phase_diag.c)
add_gate4_diag_target(gate4_ad7_contention_ab gate4_ad7_contention_ab.c)
add_gate4_diag_target(gate4_ad7_turnaround_sweep gate4_ad7_turnaround_sweep.c)
add_gate4_window_target(gate4_read_window_capture)
add_gate4_dma_capture_target(gate4_ale_reset_capture)
add_gate4_astb_edge_target(gate4_astb_edge_verify)
