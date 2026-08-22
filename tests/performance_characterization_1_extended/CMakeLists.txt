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

# AI-B0 keeps the accepted bounded current-address engine intact and compiles
# it against a separate native V30 program.  The 42-entry table is a new,
# deliberately unaccepted physical envelope; only hardware evidence may
# promote this target beyond feasibility status.
pi86_add_v30_rom(ai_bridge_mailbox_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/bios/ai_bridge_mailbox_test.asm"
    SYMBOL ai_bridge_mailbox_rom
    DEPENDS
        "${PROJECT_SOURCE_DIR}/firmware/v30/bios/include/platform.inc"
)

add_executable(ai_bridge_mailbox
    pc1c_bounded_rom_window.c
)

target_compile_definitions(ai_bridge_mailbox PRIVATE
    PI86_AI_BRIDGE_B0=1
    PC1C_TABLE_ENTRIES=42u
    PC1C_ROM_HEADER="ai_bridge_mailbox_rom.h"
    PC1C_ROM_DATA=ai_bridge_mailbox_rom_data
    PC1C_ROM_SIZE=ai_bridge_mailbox_rom_size
    PC1C_ROM_SHA256=ai_bridge_mailbox_rom_sha256
)

target_include_directories(ai_bridge_mailbox PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(ai_bridge_mailbox
    ai_bridge_mailbox_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(ai_bridge_mailbox
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(ai_bridge_mailbox
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(ai_bridge_mailbox
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(ai_bridge_mailbox
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(ai_bridge_mailbox
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(ai_bridge_mailbox 1)
pico_enable_stdio_uart(ai_bridge_mailbox 0)
pico_add_extra_outputs(ai_bridge_mailbox)

# The 0.300 MHz build above is retained as the measured linear-scan deadline
# characterization.  AI-B0 acceptance uses a longer physical T1 window while
# preserving the same ROM, table, synchronized inputs, and PIO/DMA ownership.
add_executable(ai_bridge_mailbox_200khz
    pc1c_bounded_rom_window.c
)

target_compile_definitions(ai_bridge_mailbox_200khz PRIVATE
    PI86_AI_BRIDGE_B0=1
    PC1C_V30_HZ=200000u
    PC1C_TABLE_ENTRIES=42u
    PC1C_ROM_HEADER="ai_bridge_mailbox_rom.h"
    PC1C_ROM_DATA=ai_bridge_mailbox_rom_data
    PC1C_ROM_SIZE=ai_bridge_mailbox_rom_size
    PC1C_ROM_SHA256=ai_bridge_mailbox_rom_sha256
)

target_include_directories(ai_bridge_mailbox_200khz PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(ai_bridge_mailbox_200khz
    ai_bridge_mailbox_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(ai_bridge_mailbox_200khz
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(ai_bridge_mailbox_200khz
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(ai_bridge_mailbox_200khz
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(ai_bridge_mailbox_200khz
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(ai_bridge_mailbox_200khz
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(ai_bridge_mailbox_200khz 1)
pico_enable_stdio_uart(ai_bridge_mailbox_200khz 0)
pico_add_extra_outputs(ai_bridge_mailbox_200khz)

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

pi86_add_v30_rom(same_run_ram_test_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/bios/same_run_ram_test.asm"
    SYMBOL same_run_ram_test_rom
)

add_executable(pc1c_same_run_ram
    pc1c_same_run_ram.c
)

target_include_directories(pc1c_same_run_ram PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_same_run_ram
    same_run_ram_test_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_same_run_ram
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_same_run_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_same_run_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_same_run_ram.pio
)
pico_generate_pio_header(pc1c_same_run_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_same_run_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_same_run_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_same_run_ram 1)
pico_enable_stdio_uart(pc1c_same_run_ram 0)
pico_add_extra_outputs(pc1c_same_run_ram)

pi86_add_v30_rom(multi_slot_ram_test_rom
    SOURCE "${PROJECT_SOURCE_DIR}/firmware/v30/bios/multi_slot_ram_test.asm"
    SYMBOL multi_slot_ram_test_rom
)

add_executable(pc1c_multi_slot_ram
    pc1c_multi_slot_ram.c
)

target_include_directories(pc1c_multi_slot_ram PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_multi_slot_ram
    multi_slot_ram_test_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_multi_slot_ram
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_multi_slot_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_multi_slot_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_multi_slot_ram.pio
)
pico_generate_pio_header(pc1c_multi_slot_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_multi_slot_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_multi_slot_ram
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_multi_slot_ram 1)
pico_enable_stdio_uart(pc1c_multi_slot_ram 0)
pico_add_extra_outputs(pc1c_multi_slot_ram)

# DC-A keeps the accepted B2-C bus engine on Core0 and adds a deliberately
# non-driving Core1 service role.  Its SRAM SPSC rings, overflow handling,
# heartbeat and forced service stall are exercised around the unchanged bus
# regression.  The target is separate so the accepted B2-C UF2 remains stable.
add_executable(pc1c_dual_core_foundation
    pc1c_multi_slot_ram.c
)

target_compile_definitions(pc1c_dual_core_foundation PRIVATE
    PC1C_DUAL_CORE_FOUNDATION=1
)

target_include_directories(pc1c_dual_core_foundation PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_dual_core_foundation
    multi_slot_ram_test_rom
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_dual_core_foundation
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_dual_core_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_dual_core_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_multi_slot_ram.pio
)
pico_generate_pio_header(pc1c_dual_core_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_dual_core_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_dual_core_foundation
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_dual_core_foundation 1)
pico_enable_stdio_uart(pc1c_dual_core_foundation 0)
pico_add_extra_outputs(pc1c_dual_core_foundation)

# DC-B0 retains the complete DC-A/B2-C realtime regression but removes USB
# connection and human-readable formatting from Core0. After the V30 reaches
# its safe terminal state, Core0 publishes immutable result/raw-trace ownership
# and Core1 alone waits for CDC, decodes PIO0/DMA records, and formats output.
add_executable(pc1c_dual_core_service_output
    pc1c_multi_slot_ram.c
)

target_compile_definitions(pc1c_dual_core_service_output PRIVATE
    PC1C_DUAL_CORE_FOUNDATION=1
    PC1C_DUAL_CORE_SERVICE_OUTPUT=1
)

target_include_directories(pc1c_dual_core_service_output PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_dual_core_service_output
    multi_slot_ram_test_rom
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_dual_core_service_output
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_dual_core_service_output
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_dual_core_service_output
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_multi_slot_ram.pio
)
pico_generate_pio_header(pc1c_dual_core_service_output
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_dual_core_service_output
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_dual_core_service_output
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_dual_core_service_output 1)
pico_enable_stdio_uart(pc1c_dual_core_service_output 0)
pico_add_extra_outputs(pc1c_dual_core_service_output)

# DC-B1-A replays authentic retained PIO0/DMA GPIO words through the same
# bounded realtime-to-service ring. It proves ordered active consumption and
# exact counted/nonblocking overflow while the service consumer is stalled.
# This is a transport feasibility gate; simultaneous long-running V30/CDC
# stress remains DC-B1-B.
add_executable(pc1c_dual_core_trace_backpressure
    pc1c_multi_slot_ram.c
)

target_compile_definitions(pc1c_dual_core_trace_backpressure PRIVATE
    PC1C_DUAL_CORE_FOUNDATION=1
    PC1C_DUAL_CORE_SERVICE_OUTPUT=1
    PC1C_DUAL_CORE_LIVE_TRANSPORT=1
)

target_include_directories(pc1c_dual_core_trace_backpressure PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_dual_core_trace_backpressure
    multi_slot_ram_test_rom
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_dual_core_trace_backpressure
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_dual_core_trace_backpressure
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_dual_core_trace_backpressure
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_multi_slot_ram.pio
)
pico_generate_pio_header(pc1c_dual_core_trace_backpressure
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_dual_core_trace_backpressure
    ${CMAKE_CURRENT_LIST_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pc1c_dual_core_trace_backpressure
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_enable_stdio_usb(pc1c_dual_core_trace_backpressure 1)
pico_enable_stdio_uart(pc1c_dual_core_trace_backpressure 0)
pico_add_extra_outputs(pc1c_dual_core_trace_backpressure)

# Preserve pc1c_multi_slot_ram as the B2-C regression while compiling the same
# orchestration code with a wider passive observer. This target does not run
# Epoch B or drive RAM data; it only locates the V30 byte-write data-valid phase.
add_executable(pc1c_byte_write_phase
    pc1c_multi_slot_ram.c
)

target_compile_definitions(pc1c_byte_write_phase PRIVATE
    PC1C_BYTE_WRITE_PHASE_CHARACTERIZATION=1
)

target_include_directories(pc1c_byte_write_phase PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(pc1c_byte_write_phase
    multi_slot_ram_test_rom
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_sync
)

pico_generate_pio_header(pc1c_byte_write_phase
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pc1c_byte_write_phase
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_bounded_rom_window.pio
)
pico_generate_pio_header(pc1c_byte_write_phase
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_multi_slot_ram.pio
)
pico_generate_pio_header(pc1c_byte_write_phase
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_reset_clock_qualifier.pio
)
pico_generate_pio_header(pc1c_byte_write_phase
    ${CMAKE_CURRENT_LIST_DIR}/pc1c_byte_write_phase_observer.pio
)

pico_enable_stdio_usb(pc1c_byte_write_phase 1)
pico_enable_stdio_uart(pc1c_byte_write_phase 0)
pico_add_extra_outputs(pc1c_byte_write_phase)
