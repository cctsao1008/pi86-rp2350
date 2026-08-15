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

add_gate4_memread_target(gate4_memread 0)
add_gate4_memread_target(gate4_memread_smoke 1)
