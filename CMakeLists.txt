cmake_minimum_required(VERSION 3.13)

# The repository-pinned Pico SDK submodule is the canonical SDK dependency.
# An explicit -DPICO_SDK_PATH=... may still be used for deliberate local
# experiments, but no environment variable is required for normal builds.
set(PI86_PICO_SDK_DEFAULT_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk")

if(NOT DEFINED PICO_SDK_PATH OR PICO_SDK_PATH STREQUAL "")
    set(PICO_SDK_PATH
        "${PI86_PICO_SDK_DEFAULT_PATH}"
        CACHE PATH
        "Path to the Raspberry Pi Pico SDK"
    )
endif()

if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR
        "Pico SDK submodule is missing or incomplete at:\n"
        "  ${PICO_SDK_PATH}\n\n"
        "Initialize repository dependencies with:\n"
        "  git submodule update --init --recursive\n"
    )
endif()

# Pico SDK 2.3.0 contains the official Waveshare RP2350-PiZero board
# definition. Select it before importing the SDK.
if(NOT DEFINED PICO_BOARD OR PICO_BOARD STREQUAL "")
    set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "Target board")
endif()

message(STATUS "pi86-rp2350 Pico SDK: ${PICO_SDK_PATH}")
message(STATUS "pi86-rp2350 board:    ${PICO_BOARD}")

include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")

project(pi86_rp2350 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_subdirectory(firmware)
add_subdirectory(tests/gpio_test)
add_subdirectory(tests/gate2_preflight)
