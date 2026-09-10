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

# WLINK treats newlines as whitespace while parsing directive files.  RAW has
# an optional BIN/HEX selector, so state BIN explicitly to terminate the
# FORMAT directive before the following OPTION token.  RP86 loads byte zero
# of OUTPUT at LOAD_ADDRESS; OPTION OFFSET fixes linker address calculation at
# that physical base, while OUTPUT RAW OFFSET skips the corresponding leading
# padding in the emitted binary.
file(WRITE "${LINK_SCRIPT}"
    "format raw bin\n"
    "option quiet\n"
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
    file(READ "${LINK_SCRIPT}" link_script_text)
    message(FATAL_ERROR
        "Open Watcom WLINK failed with exit code ${link_result}\n"
        "--- linker script ---\n${link_script_text}\n"
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
