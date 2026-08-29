include("${PROJECT_SOURCE_DIR}/cmake/ProcessorImage.cmake")

# The accepted 1 MHz companion image is now the canonical physical runtime.
# Keep its embedded-image symbol/header stable so the same source also remains usable by
# the retained characterization targets.
rp86_add_processor_image(processor_runtime_image
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/processor/runtime.asm"
    SYMBOL processor_runtime_image
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/processor/include/platform.inc"
)

# A standalone flat image loaded by the Host into the Internal SRAM backing.
rp86_add_processor_image(calculator_workload
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/calculator.asm"
    SYMBOL calculator_workload
)

add_executable(rp86_rp2350
    board/rp2350_pizero_resources.c
    memory/backing.c
    memory/internal_sram_backing.c
    memory/memory_service.c
    memory/memory.c
    memory/psram_backing.c
    memory/shared_mailbox.c
    processor/processor_bus.c
    runtime/clock_stepped_bus_controller.c
    runtime/execution_clock_controller.c
    runtime/runtime.c
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
add_dependencies(rp86_rp2350 calculator_workload_image)

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
    RP86_PROCESSOR_HZ=1000000
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

# First canonical native workload. It is built as a standalone flat binary
# for Host upload; it is not linked into the RP2350 firmware or presented as a
# fixed image dependency.
rp86_add_processor_image(hello
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/hello.asm"
    SYMBOL hello_workload
)

# Lifecycle validation image for the general Internal-SRAM execution path.
rp86_add_processor_image(runtime_lifecycle
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/runtime_lifecycle.asm"
    SYMBOL runtime_lifecycle_workload
)

# Polling Host/processor ownership-transfer mailbox demonstration.
rp86_add_processor_image(shared_mailbox
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/shared_mailbox.asm"
    SYMBOL shared_mailbox_workload
)
