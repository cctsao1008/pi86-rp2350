add_executable(pi86_rp2350
    board/rp2350_pizero_resources.c
    main.c
    memory/psram_backing.c
    runtime/runtime.c
)

target_include_directories(pi86_rp2350 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pi86_rp2350
    pico_stdlib
    pico_bootrom
    hardware_psram
    hardware_xip_cache
)

# The board exposes an optional APS6404L-class footprint on QMI CS1/GPIO47.
# Detect the device safely at boot instead of assuming that it is populated.
target_compile_definitions(pi86_rp2350 PRIVATE
    PICO_PSRAM_CS_PIN=47
    PICO_AUTO_DETECT_PSRAM_SIZE=1
)

pico_enable_stdio_usb(pi86_rp2350 1)
pico_enable_stdio_uart(pi86_rp2350 0)

pico_add_extra_outputs(pi86_rp2350)
