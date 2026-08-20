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
add_pc1b_pio_direct_target(pc1b_pio_direct_post_reset_epoch_sweep    300000)
target_compile_definitions(pc1b_pio_direct_post_reset_epoch_sweep PRIVATE
    PC1B_DIRECT_SWEEP=1
)

add_executable(pc1c_address_capture
    pc1c_address_capture.c
)

target_include_directories(pc1c_address_capture PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_address_capture
    pico_stdlib
    hardware_clocks
    hardware_pio
)

pico_generate_pio_header(pc1c_address_capture
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_address_capture
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bus_capture.pio
)
pico_generate_pio_header(pc1c_address_capture
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1c_address_capture 1)
pico_enable_stdio_uart(pc1c_address_capture 0)
pico_add_extra_outputs(pc1c_address_capture)

add_executable(pc1c_qualified_reset_vector
    pc1c_qualified_reset_vector.c
)

target_include_directories(pc1c_qualified_reset_vector PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_qualified_reset_vector
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_qualified_reset_vector
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_qualified_reset_vector
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_ale_observer.pio
)
pico_generate_pio_header(pc1c_qualified_reset_vector
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_pio_rom_sequencer.pio
)
pico_generate_pio_header(pc1c_qualified_reset_vector
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1c_qualified_reset_vector 1)
pico_enable_stdio_uart(pc1c_qualified_reset_vector 0)
pico_add_extra_outputs(pc1c_qualified_reset_vector)

include("${PROJECT_SOURCE_DIR}/cmake/V30Rom.cmake")

pi86_add_v30_rom(pc1c0c_sram_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/rom/pc1c0c_sram_rom.asm"
    SYMBOL pc1c0c_sram_rom
)

add_executable(pc1c_sram_rom_execution
    pc1c_sram_rom_execution.c
)

target_include_directories(pc1c_sram_rom_execution PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_sram_rom_execution
    pc1c0c_sram_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_sram_rom_execution
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_sram_rom_execution
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_pio_rom_sequencer.pio
)
pico_generate_pio_header(pc1c_sram_rom_execution
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_sram_rom_execution
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_sram_rom_execution 1)
pico_enable_stdio_uart(pc1c_sram_rom_execution 0)
pico_add_extra_outputs(pc1c_sram_rom_execution)

pi86_add_v30_rom(pc1c_native_bios_hello_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/rom/pc1c_native_bios_hello.asm"
    SYMBOL pc1c_native_bios_hello_rom
)

add_executable(pc1c_native_bios_hello
    pc1c_sram_rom_execution.c
)

target_compile_definitions(pc1c_native_bios_hello PRIVATE
    PC1C_NATIVE_BIOS_HELLO=1
    PC1C_ROM_HEADER=\"pc1c_native_bios_hello_rom.h\"
    PC1C_ROM_DATA=pc1c_native_bios_hello_rom_data
    PC1C_ROM_SIZE=pc1c_native_bios_hello_rom_size
    PC1C_ROM_SHA256=pc1c_native_bios_hello_rom_sha256
)

target_include_directories(pc1c_native_bios_hello PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_native_bios_hello
    pc1c_native_bios_hello_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_native_bios_hello
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_native_bios_hello
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_pio_rom_sequencer.pio
)
pico_generate_pio_header(pc1c_native_bios_hello
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_native_bios_hello
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_native_bios_hello 1)
pico_enable_stdio_uart(pc1c_native_bios_hello 0)
pico_add_extra_outputs(pc1c_native_bios_hello)

pi86_add_v30_rom(native_bios_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/bios/native_bios.asm"
    SYMBOL native_bios_rom
    DEPENDS
        "${PROJECT_SOURCE_DIR}/firmware/v30/bios/include/platform.inc"
        "${PROJECT_SOURCE_DIR}/firmware/v30/bios/include/diagnostic_console.inc"
)

add_executable(pc1c_native_bios_foundation
    pc1c_sram_rom_execution.c
)

target_compile_definitions(pc1c_native_bios_foundation PRIVATE
    PC1C_NATIVE_BIOS_FOUNDATION=1
    PC1C_ROM_HEADER="native_bios_rom.h"
    PC1C_ROM_DATA=native_bios_rom_data
    PC1C_ROM_SIZE=native_bios_rom_size
    PC1C_ROM_SHA256=native_bios_rom_sha256
)

target_include_directories(pc1c_native_bios_foundation PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_native_bios_foundation
    native_bios_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_native_bios_foundation
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_native_bios_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_pio_rom_sequencer.pio
)
pico_generate_pio_header(pc1c_native_bios_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_native_bios_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_native_bios_foundation 1)
pico_enable_stdio_uart(pc1c_native_bios_foundation 0)
pico_add_extra_outputs(pc1c_native_bios_foundation)

add_executable(pc1c_arbitrary_rom_feasibility
    pc1c_arbitrary_rom_feasibility.c
)

target_include_directories(pc1c_arbitrary_rom_feasibility PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_arbitrary_rom_feasibility
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_arbitrary_rom_feasibility
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_arbitrary_rom_feasibility
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_arbitrary_rom_feasibility.pio
)
pico_generate_pio_header(pc1c_arbitrary_rom_feasibility
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1c_arbitrary_rom_feasibility 1)
pico_enable_stdio_uart(pc1c_arbitrary_rom_feasibility 0)
pico_add_extra_outputs(pc1c_arbitrary_rom_feasibility)

add_executable(pc1c_arbitrary_rom_response
    pc1c_arbitrary_rom_response.c
)

target_include_directories(pc1c_arbitrary_rom_response PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_arbitrary_rom_response
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_arbitrary_rom_response
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_arbitrary_rom_response
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_arbitrary_rom_response.pio
)
pico_generate_pio_header(pc1c_arbitrary_rom_response
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)

pico_enable_stdio_usb(pc1c_arbitrary_rom_response 1)
pico_enable_stdio_uart(pc1c_arbitrary_rom_response 0)
pico_add_extra_outputs(pc1c_arbitrary_rom_response)

add_executable(pc1c_bounded_rom_window
    pc1c_bounded_rom_window.c
)

target_include_directories(pc1c_bounded_rom_window PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_bounded_rom_window
    native_bios_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_bounded_rom_window
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_bounded_rom_window
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_bounded_rom_window
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_bounded_rom_window
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_bounded_rom_window
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_bounded_rom_window 1)
pico_enable_stdio_uart(pc1c_bounded_rom_window 0)
pico_add_extra_outputs(pc1c_bounded_rom_window)

pi86_add_v30_rom(int10_stack_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/bios/int10_stack_test.asm"
    SYMBOL int10_stack_rom
)

add_executable(pc1c_int10_stack
    pc1c_int10_stack.c
)

target_include_directories(pc1c_int10_stack PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_int10_stack
    int10_stack_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_int10_stack
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_int10_stack
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_int10_stack
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_int10_stack
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_int10_stack
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_int10_stack 1)
pico_enable_stdio_uart(pc1c_int10_stack 0)
pico_add_extra_outputs(pc1c_int10_stack)
