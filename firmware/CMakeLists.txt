add_executable(pi86_rp2350
    main.c
)

target_include_directories(pi86_rp2350 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(pi86_rp2350
    pico_stdlib
)

pico_enable_stdio_usb(pi86_rp2350 1)
pico_enable_stdio_uart(pi86_rp2350 0)

pico_add_extra_outputs(pi86_rp2350)
