include("${PROJECT_SOURCE_DIR}/cmake/ProcessorImage.cmake")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
