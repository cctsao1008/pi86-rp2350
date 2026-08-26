include("${PROJECT_SOURCE_DIR}/cmake/V30Rom.cmake")

# The accepted 1 MHz companion image is now the canonical physical runtime.
# Keep its ROM symbol/header stable so the same source also remains usable by
# the retained characterization targets.
pi86_add_v30_rom(companion_runtime_rom
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/v30/bios/companion_runtime_test.asm"
    SYMBOL companion_runtime_rom
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/v30/bios/include/platform.inc"
)

set(PI86_EXTENDED_DIR
    "${PROJECT_SOURCE_DIR}/tests/performance_characterization_1_extended")

add_executable(pi86_rp2350
    board/rp2350_pizero_resources.c
    memory/backing.c
    memory/internal_sram_backing.c
    memory/psram_backing.c
    runtime/runtime.c
    runtime/workload_manager.c
    storage/flash_disk.c
    storage/flash_service.c
    storage/flash_volume.c
    ${PROJECT_SOURCE_DIR}/third_party/fatfs/source/ff.c
    ${PROJECT_SOURCE_DIR}/third_party/fatfs/source/ffunicode.c
    ${PI86_EXTENDED_DIR}/companion_runtime.c
    ${PI86_EXTENDED_DIR}/ai_bridge_usb.c
    ${PI86_EXTENDED_DIR}/ai_bridge_usb_descriptors.c
)

target_include_directories(pi86_rp2350 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${PI86_EXTENDED_DIR}
    ${PROJECT_SOURCE_DIR}/third_party/fatfs/source
)

target_link_libraries(pi86_rp2350
    companion_runtime_rom
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

target_compile_definitions(pi86_rp2350 PRIVATE
    PI86_HAS_EXTERNAL_PSRAM=$<BOOL:${PI86_HAS_EXTERNAL_PSRAM}>
    PI86_HAS_SDCARD=$<BOOL:${PI86_HAS_SDCARD}>
    PI86_HAS_DVI=$<BOOL:${PI86_HAS_DVI}>
    PI86_HAS_PIO_USB=$<BOOL:${PI86_HAS_PIO_USB}>
    PICO_FLASH_ASSUME_CORE1_SAFE=1
    PC1C_ROM_HEADER="companion_runtime_rom.h"
    PC1C_ROM_DATA=companion_runtime_rom_data
    PC1C_ROM_SIZE=companion_runtime_rom_size
    PC1C_ROM_SHA256=companion_runtime_rom_sha256
    COMPANION_V30_HZ=1000000
    PI86_CANONICAL_RUNTIME=1
    AI_BRIDGE_HID_TRANSPORT=1
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
)

# The optional APS6404L-class footprint uses QMI CS1/GPIO47. Only configure
# the SDK PSRAM probe when the build declares that the device is populated.
if(PI86_HAS_EXTERNAL_PSRAM)
    target_compile_definitions(pi86_rp2350 PRIVATE
        PICO_PSRAM_CS_PIN=47
        PICO_AUTO_DETECT_PSRAM_SIZE=1
    )
    target_link_libraries(pi86_rp2350
        hardware_psram
        hardware_xip_cache
    )
endif()

pico_enable_stdio_usb(pi86_rp2350 1)
pico_enable_stdio_uart(pi86_rp2350 0)

pico_generate_pio_header(pi86_rp2350
    ${PROJECT_SOURCE_DIR}/tests/performance_characterization_1/perf_continuous_clock.pio
)
pico_generate_pio_header(pi86_rp2350
    ${PI86_EXTENDED_DIR}/pc1c_pio_rom_sequencer.pio
)
pico_generate_pio_header(pi86_rp2350
    ${PI86_EXTENDED_DIR}/pc1c_companion_runtime.pio
)
pico_generate_pio_header(pi86_rp2350
    ${PI86_EXTENDED_DIR}/pc1b_first_cycle_phase_capture.pio
)
pico_generate_pio_header(pi86_rp2350
    ${PI86_EXTENDED_DIR}/pc1c_sram_rom_execution_observer.pio
)

pico_add_extra_outputs(pi86_rp2350)

# The final 12 MiB belongs to flash:/, so no canonical firmware may grow past
# the first 4 MiB of the 16 MiB W25Q128JV.
add_custom_command(TARGET pi86_rp2350 POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DPI86_FIRMWARE_BIN=$<TARGET_FILE_DIR:pi86_rp2350>/pi86_rp2350.bin
        -DPI86_FIRMWARE_LIMIT=4194304
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckFirmwareSize.cmake
    VERBATIM
)

# First canonical native workload. It is built as a standalone flat binary
# for Host upload; it is not linked into the RP2350 firmware or presented as a
# fixed ROM dependency.
pi86_add_v30_rom(hello
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/hello.asm"
    SYMBOL hello_workload
)
