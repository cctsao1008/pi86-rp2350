add_executable(pi86_rp2350
    board/rp2350_pizero_resources.c
    main.c
    memory/psram_backing.c
    runtime/runtime.c
    runtime/workload_manager.c
)

target_include_directories(pi86_rp2350 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pi86_rp2350
    pico_stdlib
    pico_bootrom
)

target_compile_definitions(pi86_rp2350 PRIVATE
    PI86_HAS_EXTERNAL_PSRAM=$<BOOL:${PI86_HAS_EXTERNAL_PSRAM}>
    PI86_HAS_SDCARD=$<BOOL:${PI86_HAS_SDCARD}>
    PI86_HAS_DVI=$<BOOL:${PI86_HAS_DVI}>
    PI86_HAS_PIO_USB=$<BOOL:${PI86_HAS_PIO_USB}>
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

pico_add_extra_outputs(pi86_rp2350)

# First canonical native workload. It is built as a standalone flat binary
# for Host upload; it is not linked into the RP2350 firmware or presented as a
# fixed ROM dependency.
include("${PROJECT_SOURCE_DIR}/cmake/V30Rom.cmake")
pi86_add_v30_rom(hello
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/hello.asm"
    SYMBOL hello_workload
)
