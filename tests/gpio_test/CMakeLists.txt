add_executable(gpio_test
    gpio_test.c
)

target_include_directories(gpio_test PRIVATE
    ${PROJECT_SOURCE_DIR}/firmware
)

target_link_libraries(gpio_test
    pico_stdlib
)

pico_enable_stdio_usb(gpio_test 1)
pico_enable_stdio_uart(gpio_test 0)

pico_add_extra_outputs(gpio_test)
