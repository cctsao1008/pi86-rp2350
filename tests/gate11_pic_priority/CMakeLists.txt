add_executable(gate11_pic_priority
    gate11_pic_priority.c
    ${PROJECT_SOURCE_DIR}/firmware/pic/pic.c
)

target_include_directories(gate11_pic_priority PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gate11_pic_priority
    pico_stdlib
)

pico_enable_stdio_usb(gate11_pic_priority 1)
pico_enable_stdio_uart(gate11_pic_priority 0)

pico_add_extra_outputs(gate11_pic_priority)
