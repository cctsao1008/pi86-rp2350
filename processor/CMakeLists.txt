include("${PROJECT_SOURCE_DIR}/cmake/ProcessorImage.cmake")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

option(RP86_ENABLE_PROCESSOR_C16
       "Enable experimental Open Watcom C/16 processor workloads" OFF)
option(RP86_ENABLE_FREERTOS_8086
       "Enable the native FreeRTOS Intel 8086 validation workloads" OFF)

if(RP86_ENABLE_PROCESSOR_C16 OR RP86_ENABLE_FREERTOS_8086)
    include("${PROJECT_SOURCE_DIR}/cmake/ProcessorC16Image.cmake")
endif()

# Canonical resident program executed by the physical 8086-class processor.
rp86_add_processor_image(processor_runtime_image
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/runtime/runtime.asm"
    SYMBOL processor_runtime_image
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/runtime/include/platform.inc"
)

# Maintained workload shipped as part of the RP86 runtime.
rp86_add_processor_image(calculator_workload
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/builtins/calculator/calculator.asm"
    SYMBOL calculator_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/builtins/calculator/workload.json"
    PACKAGE_NAME "CALC.P86W"
)

# Small public examples of the workload and service interfaces.
rp86_add_processor_image(hello
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/examples/hello/hello.asm"
    SYMBOL hello_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/examples/hello/workload.json"
    PACKAGE_NAME "HELLO.P86W"
)

rp86_add_processor_image(shared_mailbox
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/examples/shared-mailbox/shared_mailbox.asm"
    SYMBOL shared_mailbox_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/examples/shared-mailbox/workload.json"
    PACKAGE_NAME "MAILBOX.P86W"
)

rp86_add_processor_image(fast_invsqrt
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/examples/fast-invsqrt/fast-invsqrt.asm"
    SYMBOL fast_invsqrt_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/examples/fast-invsqrt/workload.json"
    PACKAGE_NAME "INVSQRT.P86W"
)

# Physical validation programs remain buildable and packageable, but are not
# presented as end-user examples or built-in runtime services.
rp86_add_processor_image(runtime_lifecycle
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/runtime_lifecycle.asm"
    SYMBOL runtime_lifecycle_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/runtime_lifecycle.json"
    PACKAGE_NAME "LIFECYCLE.P86W"
)

rp86_add_processor_image(clock_stepped_general
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/clock_stepped_general.asm"
    SYMBOL clock_stepped_general_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/clock_stepped_general.json"
    PACKAGE_NAME "CLOCKGEN.P86W"
)

rp86_add_processor_image(execution_clock_transition_workload
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/execution_clock_transition.asm"
    SYMBOL execution_clock_transition_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/execution_clock_transition.json"
    PACKAGE_NAME "CLOCKSW.P86W"
)

rp86_add_processor_image(periodic_tick
    SOURCE "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/periodic_tick.asm"
    SYMBOL periodic_tick_workload
    METADATA "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/periodic_tick.json"
    PACKAGE_NAME "TICK.P86W"
)

if(RP86_ENABLE_PROCESSOR_C16)
    # Issue #58 validation target.  This path is intentionally opt-in so the
    # normal RP86 firmware build remains independent of Open Watcom.
    rp86_add_processor_c16_image(c16_abi_smoke
        C_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/c16_abi_smoke/main.c"
        ASM_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/c16_abi_smoke/startup.asm"
        METADATA
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/c16_abi_smoke/workload.json"
        PACKAGE_NAME "C16SMOKE.P86W"
        LOAD_ADDRESS "0x10000"
    )
endif()

if(RP86_ENABLE_FREERTOS_8086)
    set(RP86_FREERTOS_KERNEL_DIR "${PROJECT_SOURCE_DIR}/third_party/FreeRTOS-Kernel")
    set(RP86_FREERTOS_PORT_CONFIG "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_port/FreeRTOSConfig.h")
    set(RP86_FREERTOS_SYSTEM_CONFIG "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_system/FreeRTOSConfig.h")
    if(NOT EXISTS "${RP86_FREERTOS_KERNEL_DIR}/include/FreeRTOS.h")
        message(FATAL_ERROR
            "FreeRTOS-Kernel submodule is missing. Initialize it with:\n"
            "  git submodule update --init third_party/FreeRTOS-Kernel"
        )
    endif()

    # ProcessorC16Image prepends each ASM object.  This list is deliberately
    # ordered so startup remains first in the final link and rp86_c16_entry is
    # still exactly 1000:0000.
    rp86_add_processor_c16_image(freertos_port_validation
        C_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_port/main.c"
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086/port.c"
            "${RP86_FREERTOS_KERNEL_DIR}/list.c"
            "${RP86_FREERTOS_KERNEL_DIR}/queue.c"
            "${RP86_FREERTOS_KERNEL_DIR}/tasks.c"
            "${RP86_FREERTOS_KERNEL_DIR}/portable/MemMang/heap_1.c"
        ASM_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086/compiler_helpers.asm"
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086/portasm.asm"
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_port/startup.asm"
        INCLUDE_DIRS
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_port"
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086"
            "${RP86_FREERTOS_KERNEL_DIR}/include"
        C_OPTIONS
            "-fi=${RP86_FREERTOS_PORT_CONFIG}"
        DEPENDS
            "${RP86_FREERTOS_PORT_CONFIG}"
        METADATA
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_port/workload.json"
        PACKAGE_NAME "FREERTOS.P86W"
        LOAD_ADDRESS "0x10000"
    )

    # Issue #67/#61 sustained system workload.  It shares the proven portable
    # layer but keeps its application, configuration, package, and evidence
    # separate from the closed #60 port witness.
    rp86_add_processor_c16_image(freertos_system_validation
        C_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_system/main.c"
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086/port.c"
            "${RP86_FREERTOS_KERNEL_DIR}/list.c"
            "${RP86_FREERTOS_KERNEL_DIR}/queue.c"
            "${RP86_FREERTOS_KERNEL_DIR}/tasks.c"
            "${RP86_FREERTOS_KERNEL_DIR}/portable/MemMang/heap_1.c"
        ASM_SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086/compiler_helpers.asm"
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086/portasm.asm"
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_system/startup.asm"
        INCLUDE_DIRS
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_system"
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/ports/freertos/8086"
            "${RP86_FREERTOS_KERNEL_DIR}/include"
        C_OPTIONS
            "-fi=${RP86_FREERTOS_SYSTEM_CONFIG}"
        DEPENDS
            "${RP86_FREERTOS_SYSTEM_CONFIG}"
        METADATA
            "${CMAKE_CURRENT_LIST_DIR}/workloads/validation/freertos_system/workload.json"
        PACKAGE_NAME "FREERTOS-SYSTEM.P86W"
        LOAD_ADDRESS "0x10000"
    )
endif()
