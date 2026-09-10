if(NOT DEFINED WLINK_EXECUTABLE OR WLINK_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "WLINK_EXECUTABLE is required")
endif()
if(NOT DEFINED OUTPUT OR OUTPUT STREQUAL "")
    message(FATAL_ERROR "OUTPUT is required")
endif()
if(NOT DEFINED MAP OR MAP STREQUAL "")
    message(FATAL_ERROR "MAP is required")
endif()
if(NOT DEFINED LINK_SCRIPT OR LINK_SCRIPT STREQUAL "")
    message(FATAL_ERROR "LINK_SCRIPT is required")
endif()
if(NOT DEFINED LOAD_ADDRESS OR LOAD_ADDRESS STREQUAL "")
    message(FATAL_ERROR "LOAD_ADDRESS is required")
endif()
if(NOT DEFINED OBJECTS OR OBJECTS STREQUAL "")
    message(FATAL_ERROR "OBJECTS is required")
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

# WLINK's raw format still performs normal segment/fixup calculation.  RP86
# loads byte zero of OUTPUT at LOAD_ADDRESS, so link at the physical base while
# asking raw output to start at the same offset rather than emitting leading
# padding.  Issue #58 keeps this contract provisional until map/binary evidence
# proves the intended semantics.
file(WRITE "${LINK_SCRIPT}"
    "option quiet\n"
    "format raw\n"
    "option offset=${LOAD_ADDRESS}\n"
    "output raw offset=${LOAD_ADDRESS}\n"
    "option map=\"${MAP}\"\n"
    "name \"${OUTPUT}\"\n"
)

foreach(object_path IN LISTS OBJECTS)
    file(APPEND "${LINK_SCRIPT}" "file \"${object_path}\"\n")
endforeach()

execute_process(
    COMMAND "${WLINK_EXECUTABLE}" "@${LINK_SCRIPT}"
    RESULT_VARIABLE link_result
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
)

if(NOT link_result EQUAL 0)
    message(FATAL_ERROR
        "Open Watcom WLINK failed with exit code ${link_result}\n"
        "--- linker script ---\n"
        "${LINK_SCRIPT}\n"
        "--- stdout ---\n${link_stdout}\n"
        "--- stderr ---\n${link_stderr}\n"
    )
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "WLINK reported success but did not produce ${OUTPUT}")
endif()
if(NOT EXISTS "${MAP}")
    message(FATAL_ERROR "WLINK reported success but did not produce ${MAP}")
endif()
