cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH is not set. Point it to a Raspberry Pi Pico SDK 2.3.0+ checkout.")
endif()

# Pico SDK 2.3.0 added an official board definition for this exact board.
# Set it before importing the SDK so the correct RP2350B package and 16 MB
# flash configuration are selected by default.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "Target board")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pi86_rp2350 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(firmware)
add_subdirectory(tests/gpio_test)
