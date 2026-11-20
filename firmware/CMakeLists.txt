add_executable(rp86_rp2350
    board/rp2350_pizero_resources.c
    memory/backing.c
    memory/internal_sram_backing.c
    memory/memory_service.c
    memory/memory.c
    memory/psram_backing.c
    memory/shared_mailbox.c
    bus/prepared_responder.c
    bus/processor_bus.c
    runtime/clock_stepped_bus_controller.c
    runtime/canonical_runtime.c
    runtime/cdc_command_parser.c
    runtime/evidence_queue.c
    runtime/execution_clock_controller.c
    runtime/host_service_dispatch.c
    runtime/runtime_context.c
    runtime/workload_executor.c
    runtime/prepared_runtime.c
    runtime/runtime_status.c
    runtime/workload_manager.c
    storage/flash_disk.c
    storage/flash_service.c
    storage/flash_volume.c
    ${PROJECT_SOURCE_DIR}/third_party/fatfs/source/ff.c
    ${PROJECT_SOURCE_DIR}/third_party/fatfs/source/ffunicode.c
    main.c
    host_protocol/usb_transport.c
    host_protocol/usb_descriptors.c
)

# The canonical firmware build also produces the maintained calculator
# package. Other examples and validation packages are part of the default
# all-target build but are not firmware dependencies.
add_dependencies(rp86_rp2350 calculator_workload_package)

target_include_directories(rp86_rp2350 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/host_protocol
    ${PROJECT_SOURCE_DIR}/third_party/fatfs/source
)

target_link_libraries(rp86_rp2350
    processor_runtime_image
    pico_stdlib
    pico_bootrom
    pico_flash
    pico_multicore
    pico_unique_id
    hardware_clocks
    hardware_dma
    hardware_flash
    hardware_pio
    hardware_sync
    hardware_watchdog
    tinyusb_device
)

target_compile_definitions(rp86_rp2350 PRIVATE
    RP86_HAS_EXTERNAL_PSRAM=$<BOOL:${RP86_HAS_EXTERNAL_PSRAM}>
    RP86_HAS_SDCARD=$<BOOL:${RP86_HAS_SDCARD}>
    RP86_HAS_DVI=$<BOOL:${RP86_HAS_DVI}>
    RP86_HAS_PIO_USB=$<BOOL:${RP86_HAS_PIO_USB}>
    PICO_FLASH_ASSUME_CORE1_SAFE=1
    RP86_PROCESSOR_IMAGE_HEADER="processor_runtime_image.h"
    RP86_PROCESSOR_IMAGE_DATA=processor_runtime_image_data
    RP86_PROCESSOR_IMAGE_SIZE=processor_runtime_image_size
    RP86_PROCESSOR_IMAGE_SHA256=processor_runtime_image_sha256
    RP86_PROCESSOR_HZ=${RP86_PROCESSOR_HZ}
    RP86_CANONICAL_RUNTIME=1
    RP86_HOST_PROTOCOL_HID_TRANSPORT=1
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
)

# The optional APS6404L-class footprint uses QMI CS1/GPIO47. Only configure
# the SDK PSRAM probe when the build declares that the device is populated.
if(RP86_HAS_EXTERNAL_PSRAM)
    target_compile_definitions(rp86_rp2350 PRIVATE
        PICO_PSRAM_CS_PIN=47
        PICO_AUTO_DETECT_PSRAM_SIZE=1
    )
    target_link_libraries(rp86_rp2350
        hardware_psram
        hardware_xip_cache
    )
endif()

pico_enable_stdio_usb(rp86_rp2350 1)
pico_enable_stdio_uart(rp86_rp2350 0)

# The physical processor runtime is timing-sensitive: DMA IRQ reloads,
# prepared-runtime classification, and clock-stepped bus service must not
# depend on XIP cache state or on the firmware's placement in NOR flash.
# The image still boots from flash, then executes entirely from RP2350 SRAM.
pico_set_binary_type(rp86_rp2350 copy_to_ram)

pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/free_running_clock.pio
)
pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/clock_stepped_clock.pio
)
pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/execution_clock_control.pio
)
pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/prepared_bus_responder.pio
)
pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/processor_service.pio
)
pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/bus_phase_capture.pio
)
pico_generate_pio_header(rp86_rp2350
    ${CMAKE_CURRENT_LIST_DIR}/runtime/bus_observer.pio
)

pico_add_extra_outputs(rp86_rp2350)

# The final 12 MiB belongs to flash:/, so no canonical firmware may grow past
# the first 4 MiB of the 16 MiB W25Q128JV.
add_custom_command(TARGET rp86_rp2350 POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DRP86_FIRMWARE_BIN=$<TARGET_FILE_DIR:rp86_rp2350>/rp86_rp2350.bin
        -DRP86_FIRMWARE_LIMIT=4194304
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckFirmwareSize.cmake
    VERBATIM
)

# Issue #106 engineering laboratories. EXCLUDE_FROM_ALL keeps them out of the
# production firmware/default build while making the repeated hardware-paced
# DMA alias experiments reproducible with explicit target builds.
add_executable(rp86_issue106_dma_alias_read_lab EXCLUDE_FROM_ALL
    labs/issue106_dma_alias_read.c
)

target_link_libraries(rp86_issue106_dma_alias_read_lab
    pico_stdlib
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_dma_alias_read_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_dma_alias_read.pio
)

pico_enable_stdio_usb(rp86_issue106_dma_alias_read_lab 1)
pico_enable_stdio_uart(rp86_issue106_dma_alias_read_lab 0)
pico_set_binary_type(rp86_issue106_dma_alias_read_lab copy_to_ram)
pico_add_extra_outputs(rp86_issue106_dma_alias_read_lab)

add_executable(rp86_issue106_dma_alias_write_lab EXCLUDE_FROM_ALL
    labs/issue106_dma_alias_write.c
)

target_link_libraries(rp86_issue106_dma_alias_write_lab
    pico_stdlib
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_dma_alias_write_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_dma_alias_write.pio
)

pico_enable_stdio_usb(rp86_issue106_dma_alias_write_lab 1)
pico_enable_stdio_uart(rp86_issue106_dma_alias_write_lab 0)
pico_set_binary_type(rp86_issue106_dma_alias_write_lab copy_to_ram)
pico_add_extra_outputs(rp86_issue106_dma_alias_write_lab)

# Phase C1a proves the PIO scattered-GPIO permutation primitive using synthetic
# raw GPIO0..27 snapshots. Physical pad capture is intentionally deferred to
# the next timing-bound front-end experiment.
add_executable(rp86_issue106_gpio_repack_lab EXCLUDE_FROM_ALL
    labs/issue106_gpio_repack.c
)

target_include_directories(rp86_issue106_gpio_repack_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(rp86_issue106_gpio_repack_lab
    pico_stdlib
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_gpio_repack_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_gpio_repack.pio
)

pico_enable_stdio_usb(rp86_issue106_gpio_repack_lab 1)
pico_enable_stdio_uart(rp86_issue106_gpio_repack_lab 0)
pico_set_binary_type(rp86_issue106_gpio_repack_lab copy_to_ram)
pico_add_extra_outputs(rp86_issue106_gpio_repack_lab)


# Phase C1b replaces the synthetic raw snapshot with a real passive ASTB/T1
# GPIO-bank capture, then runs the same proven scattered-address permutation.
add_executable(rp86_issue106_physical_t1_repack_lab EXCLUDE_FROM_ALL
    labs/issue106_physical_t1_repack.c
)

target_include_directories(rp86_issue106_physical_t1_repack_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(rp86_issue106_physical_t1_repack_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_physical_t1_repack_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_physical_t1_repack.pio
)

pico_generate_pio_header(rp86_issue106_physical_t1_repack_lab
    ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
)

pico_enable_stdio_usb(rp86_issue106_physical_t1_repack_lab 1)
pico_enable_stdio_uart(rp86_issue106_physical_t1_repack_lab 0)
pico_set_binary_type(rp86_issue106_physical_t1_repack_lab copy_to_ram)
pico_add_extra_outputs(rp86_issue106_physical_t1_repack_lab)


# Phase C2+C3 integrates the real T1 frontend with hardware lane
# classification and a 64 KiB mirrored internal-SRAM DMA read path.
add_executable(rp86_issue106_c23_integration_lab EXCLUDE_FROM_ALL
    labs/issue106_c23_integration.c
)

target_include_directories(rp86_issue106_c23_integration_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(rp86_issue106_c23_integration_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_c23_integration_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c23_integration.pio
)

pico_generate_pio_header(rp86_issue106_c23_integration_lab
    ${CMAKE_CURRENT_LIST_DIR}/runtime/free_running_clock.pio
)

pico_enable_stdio_usb(rp86_issue106_c23_integration_lab 1)
pico_enable_stdio_uart(rp86_issue106_c23_integration_lab 0)
pico_set_binary_type(rp86_issue106_c23_integration_lab copy_to_ram)
pico_add_extra_outputs(rp86_issue106_c23_integration_lab)


# Phase C4 one-shot end-to-end read-response timing experiment.
# Services only the first reset-vector WORD read and records physical
# T1/AF/R1/F1/R2/F2/R3 evidence.
add_executable(rp86_issue106_c4_response_timing_lab EXCLUDE_FROM_ALL
    labs/issue106_c4_response_timing.c
)

target_include_directories(rp86_issue106_c4_response_timing_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(rp86_issue106_c4_response_timing_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_c4_response_timing_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c4_response_timing.pio
)

pico_generate_pio_header(rp86_issue106_c4_response_timing_lab
    ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
)

pico_enable_stdio_usb(rp86_issue106_c4_response_timing_lab 1)
pico_enable_stdio_uart(rp86_issue106_c4_response_timing_lab 0)

# C4 intentionally keeps M33 code in XIP flash. The measured active path is
# entirely PIO+DMA; the 64 KiB backing window and 256 KiB data->GPIO LUT stay
# in internal SRAM. Copying the whole executable into SRAM would consume the
# same bounded-memory resource being measured and overcommit RP2350 SRAM.
pico_add_extra_outputs(rp86_issue106_c4_response_timing_lab)


# C4 frequency variants for Intel P8086-2 fixed-READY timing characterization.
# The 1 MHz target remains the empirical baseline. These variants keep the
# dataplane identical and only change the free-running processor clock.
foreach(_mhz IN ITEMS 2 4 6 8)
    set(_target rp86_issue106_c4_response_timing_${_mhz}mhz_lab)

    add_executable(${_target} EXCLUDE_FROM_ALL
        labs/issue106_c4_response_timing.c
    )

    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_compile_definitions(${_target} PRIVATE
        ISSUE106_PROCESSOR_HZ=${_mhz}000000u
    )

    target_link_libraries(${_target}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c4_response_timing.pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
    )

    pico_enable_stdio_usb(${_target} 1)
    pico_enable_stdio_uart(${_target} 0)
    pico_add_extra_outputs(${_target})
endforeach()


# Phase C5A speed-first dynamic WORD-read timing experiment.
# Replaces the C4 serial address repack with two parallel 14-bit raw-GPIO LUT
# frontends. The backing stores pre-encoded scattered-GPIO response words so
# this gate isolates address translation + SRAM + physical response latency.
foreach(_mhz IN ITEMS 4 6)
    set(_target rp86_issue106_c5_fast_read_${_mhz}mhz_lab)

    add_executable(${_target} EXCLUDE_FROM_ALL
        labs/issue106_c5_fast_read.c
    )

    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_compile_definitions(${_target} PRIVATE
        ISSUE106_PROCESSOR_HZ=${_mhz}000000u
    )

    target_link_libraries(${_target}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c5_fast_read.pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
    )

    pico_enable_stdio_usb(${_target} 1)
    pico_enable_stdio_uart(${_target} 0)

    # Keep code in XIP: C5A intentionally spends SRAM on two 64 KiB address
    # LUTs plus a 128 KiB pre-encoded WORD backing for the speed feasibility
    # gate. This representation is not the final canonical memory format.
    pico_add_extra_outputs(${_target})
endforeach()


# Phase C5B parallel-lookup latency probe.
# Same C5 speed-first data representation, but both 14-bit lookup legs are
# armed before RESET release. Low leg is high-priority; high leg is
# low-priority and triggers combine when it completes. This intentionally
# probes whether removing the C5A low->high chain recovers the missing
# half-cycle at 6 MHz.
foreach(_mhz IN ITEMS 4 6)
    set(_target rp86_issue106_c5b_parallel_read_${_mhz}mhz_lab)

    add_executable(${_target} EXCLUDE_FROM_ALL
        labs/issue106_c5_fast_read.c
    )

    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_compile_definitions(${_target} PRIVATE
        ISSUE106_PROCESSOR_HZ=${_mhz}000000u
        ISSUE106_PARALLEL_LOOKUPS=1
    )

    target_link_libraries(${_target}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c5_fast_read.pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
    )

    pico_enable_stdio_usb(${_target} 1)
    pico_enable_stdio_uart(${_target} 0)
    pico_add_extra_outputs(${_target})
endforeach()


# C5 tail-latency decomposition probes.
# T0: T1 -> direct encoded-data DMA -> responder.
# T1: T1 -> fixed SRAM pointer control DMA -> SRAM DMA -> responder.
# These keep the same physical timing observer and R2 acceptance boundary.
foreach(_mode IN ITEMS 0 1 2)
    foreach(_mhz IN ITEMS 4 6)
        set(_target rp86_issue106_c5_tail_t${_mode}_${_mhz}mhz_lab)

        add_executable(${_target} EXCLUDE_FROM_ALL
            labs/issue106_c5_tail_timing.c
        )

        target_include_directories(${_target} PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}
        )

        target_compile_definitions(${_target} PRIVATE
            ISSUE106_PROCESSOR_HZ=${_mhz}000000u
            ISSUE106_TAIL_MODE=${_mode}
        )

        target_link_libraries(${_target}
            pico_stdlib
            hardware_clocks
            hardware_dma
            hardware_pio
        )

        pico_generate_pio_header(${_target}
            ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c5_tail_timing.pio
        )

        pico_generate_pio_header(${_target}
            ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
        )

        pico_enable_stdio_usb(${_target} 1)
        pico_enable_stdio_uart(${_target} 0)
        pico_add_extra_outputs(${_target})
    endforeach()
endforeach()


# Phase C5C fused-sum latency probe.
# Keeps the C5A serialized lookup ordering, but removes the separate two-word
# combine DMA: low_lookup writes the low contribution directly to SNIFF_DATA,
# high_lookup is sniffed in SUM mode and chains directly to pointer_dma.
foreach(_mhz IN ITEMS 4 6)
    set(_target rp86_issue106_c5c_fused_read_${_mhz}mhz_lab)

    add_executable(${_target} EXCLUDE_FROM_ALL
        labs/issue106_c5_fast_read.c
    )

    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_compile_definitions(${_target} PRIVATE
        ISSUE106_PROCESSOR_HZ=${_mhz}000000u
        ISSUE106_FUSED_SUM=1
    )

    target_link_libraries(${_target}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_c5_fast_read.pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
    )

    pico_enable_stdio_usb(${_target} 1)
    pico_enable_stdio_uart(${_target} 0)
    pico_add_extra_outputs(${_target})
endforeach()


# Passive 8 MHz C1b probe: physical T1 capture/repack only.
add_executable(rp86_issue106_physical_t1_repack_8mhz_lab EXCLUDE_FROM_ALL
    labs/issue106_physical_t1_repack.c
)

target_include_directories(rp86_issue106_physical_t1_repack_8mhz_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(rp86_issue106_physical_t1_repack_8mhz_lab PRIVATE
    ISSUE106_PROCESSOR_HZ=8000000u
)

target_link_libraries(rp86_issue106_physical_t1_repack_8mhz_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_physical_t1_repack_8mhz_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_physical_t1_repack.pio
)

pico_generate_pio_header(rp86_issue106_physical_t1_repack_8mhz_lab
    ${CMAKE_CURRENT_LIST_DIR}/runtime/intel_8086_clock.pio
)

pico_enable_stdio_usb(rp86_issue106_physical_t1_repack_8mhz_lab 1)
pico_enable_stdio_uart(rp86_issue106_physical_t1_repack_8mhz_lab 0)
pico_add_extra_outputs(rp86_issue106_physical_t1_repack_8mhz_lab)


# Hardware-synchronous Intel RESET + first-T1 aperture diagnostics.
# Keep the probe identical across the sweep so frequency is the only variable.
foreach(_mhz IN ITEMS 2 4 6 7 8)
    set(_target rp86_issue106_reset_raw_trace_${_mhz}mhz_lab)

    add_executable(${_target} EXCLUDE_FROM_ALL
        labs/issue106_reset_raw_trace.c
    )

    target_include_directories(${_target} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    math(EXPR _processor_hz "${_mhz} * 1000000")
    target_compile_definitions(${_target} PRIVATE
        ISSUE106_PROCESSOR_HZ=${_processor_hz}u
    )

    target_link_libraries(${_target}
        pico_stdlib
        hardware_clocks
        hardware_dma
        hardware_pio
    )

    pico_generate_pio_header(${_target}
        ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_intel_clock_trace.pio
    )

    pico_enable_stdio_usb(${_target} 1)
    pico_enable_stdio_uart(${_target} 0)
    pico_add_extra_outputs(${_target})
endforeach()





# Eighth-point discriminator between 7.0 MHz PASS and 7.25 MHz FAIL.
add_executable(rp86_issue106_reset_raw_trace_7_125mhz_lab EXCLUDE_FROM_ALL
    labs/issue106_reset_raw_trace.c
)

target_include_directories(rp86_issue106_reset_raw_trace_7_125mhz_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(rp86_issue106_reset_raw_trace_7_125mhz_lab PRIVATE
    ISSUE106_PROCESSOR_HZ=7125000u
)

target_link_libraries(rp86_issue106_reset_raw_trace_7_125mhz_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_reset_raw_trace_7_125mhz_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_intel_clock_trace.pio
)

pico_enable_stdio_usb(rp86_issue106_reset_raw_trace_7_125mhz_lab 1)
pico_enable_stdio_uart(rp86_issue106_reset_raw_trace_7_125mhz_lab 0)
pico_add_extra_outputs(rp86_issue106_reset_raw_trace_7_125mhz_lab)
# Quarter-point discriminator between 7.0 MHz PASS and 7.5 MHz FAIL.
add_executable(rp86_issue106_reset_raw_trace_7_25mhz_lab EXCLUDE_FROM_ALL
    labs/issue106_reset_raw_trace.c
)

target_include_directories(rp86_issue106_reset_raw_trace_7_25mhz_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(rp86_issue106_reset_raw_trace_7_25mhz_lab PRIVATE
    ISSUE106_PROCESSOR_HZ=7250000u
)

target_link_libraries(rp86_issue106_reset_raw_trace_7_25mhz_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_reset_raw_trace_7_25mhz_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_intel_clock_trace.pio
)

pico_enable_stdio_usb(rp86_issue106_reset_raw_trace_7_25mhz_lab 1)
pico_enable_stdio_uart(rp86_issue106_reset_raw_trace_7_25mhz_lab 0)
pico_add_extra_outputs(rp86_issue106_reset_raw_trace_7_25mhz_lab)
# Midpoint discriminator between 7 MHz PASS and 8 MHz FAIL.
add_executable(rp86_issue106_reset_raw_trace_7_5mhz_lab EXCLUDE_FROM_ALL
    labs/issue106_reset_raw_trace.c
)

target_include_directories(rp86_issue106_reset_raw_trace_7_5mhz_lab PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(rp86_issue106_reset_raw_trace_7_5mhz_lab PRIVATE
    ISSUE106_PROCESSOR_HZ=7500000u
)

target_link_libraries(rp86_issue106_reset_raw_trace_7_5mhz_lab
    pico_stdlib
    hardware_clocks
    hardware_dma
    hardware_pio
)

pico_generate_pio_header(rp86_issue106_reset_raw_trace_7_5mhz_lab
    ${CMAKE_CURRENT_LIST_DIR}/labs/issue106_intel_clock_trace.pio
)

pico_enable_stdio_usb(rp86_issue106_reset_raw_trace_7_5mhz_lab 1)
pico_enable_stdio_uart(rp86_issue106_reset_raw_trace_7_5mhz_lab 0)
pico_add_extra_outputs(rp86_issue106_reset_raw_trace_7_5mhz_lab)

