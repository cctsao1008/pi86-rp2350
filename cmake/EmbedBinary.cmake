foreach(required_var INPUT OUTPUT_C OUTPUT_H SYMBOL)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Input binary does not exist: ${INPUT}")
endif()

file(READ "${INPUT}" binary_hex HEX)
string(LENGTH "${binary_hex}" hex_length)
math(EXPR binary_size "${hex_length} / 2")
file(SHA256 "${INPUT}" binary_sha256)

set(byte_rows "")
if(binary_size GREATER 0)
    math(EXPR last_byte "${binary_size} - 1")
    foreach(index RANGE 0 ${last_byte})
        math(EXPR hex_offset "${index} * 2")
        string(SUBSTRING "${binary_hex}" ${hex_offset} 2 byte_hex)
        string(APPEND byte_rows "0x${byte_hex},")
        math(EXPR row_position "(${index} + 1) % 12")
        if(row_position EQUAL 0)
            string(APPEND byte_rows "\n    ")
        else()
            string(APPEND byte_rows " ")
        endif()
    endforeach()
endif()

get_filename_component(header_name "${OUTPUT_H}" NAME)
file(WRITE "${OUTPUT_H}"
"#pragma once\n\n"
"#include <stddef.h>\n"
"#include <stdint.h>\n\n"
"extern const uint8_t ${SYMBOL}_data[];\n"
"extern const size_t ${SYMBOL}_size;\n"
"extern const char ${SYMBOL}_sha256[];\n"
)

file(WRITE "${OUTPUT_C}"
"#include \"${header_name}\"\n\n"
"const uint8_t ${SYMBOL}_data[] __attribute__((aligned(4))) = {\n"
"    ${byte_rows}\n"
"};\n\n"
"const size_t ${SYMBOL}_size = ${binary_size}u;\n"
"const char ${SYMBOL}_sha256[] = \"${binary_sha256}\";\n"
)

message(STATUS
    "Processor image ${SYMBOL}: ${binary_size} bytes, SHA-256 ${binary_sha256}"
)
