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

math(EXPR load_segment "${LOAD_ADDRESS} / 16")
math(EXPR load_offset "${LOAD_ADDRESS} % 16")
if(NOT load_offset EQUAL 0)
    message(FATAL_ERROR
        "C/16 LOAD_ADDRESS must be paragraph aligned; got ${LOAD_ADDRESS}"
    )
endif()

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")

# Do not use FORMAT RAW for 16-bit segmented workloads.  In WLINK, MK_RAW is
# a flat-memory format and rejects FIX_BASE (segment) relocations.  Instead,
# establish the normal 16-bit DOS segmented address model, place the CODE
# class at the RP86 physical workload segment, then use OUTPUT RAW to override
# only the emitted file representation.  OUTPUT RAW OFFSET removes the leading
# physical-address padding without changing linker address calculations.
#
# ORDER is the WLINK mechanism intended for fixed-address/ROMable targets.  It
# keeps _TEXT first at LOAD_ADDRESS while allowing the C compiler's DGROUP
# classes to follow with normal 8086 segment fixups resolved by the linker.
file(WRITE "${LINK_SCRIPT}"
    "format dos\n"
    "option quiet\n"
    "option nodefaultlibs\n"
    "option start=rp86_c16_entry\n"
    "order clname CODE segaddr=${load_segment} segment _TEXT clname DATA clname BSS\n"
    "output raw offset=${LOAD_ADDRESS}\n"
    "option map='${MAP}'\n"
    "name '${OUTPUT}'\n"
)

foreach(object_path IN LISTS OBJECTS)
    file(APPEND "${LINK_SCRIPT}" "file '${object_path}'\n")
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
