foreach(required_var NASM_EXECUTABLE SOURCE OUTPUT INCLUDE_DIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

if(NOT EXISTS "${NASM_EXECUTABLE}")
    message(FATAL_ERROR
        "Repository-local NASM 3.02 was not found at:\n"
        "  ${NASM_EXECUTABLE}\n\n"
        "Run from the repository root:\n"
        "  ./scripts/bootstrap_nasm.sh"
    )
endif()

execute_process(
    COMMAND "${NASM_EXECUTABLE}" -v
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
    ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT version_result EQUAL 0 OR
   NOT version_output MATCHES "^NASM version 3\\.02")
    message(FATAL_ERROR
        "Expected repository-local NASM 3.02, got:\n"
        "  ${version_output}${version_error}"
    )
endif()

get_filename_component(source_dir "${SOURCE}" DIRECTORY)

execute_process(
    COMMAND "${NASM_EXECUTABLE}" -f bin -Wall -Werror
            "-I${source_dir}/" "-I${INCLUDE_DIR}/"
            -o "${OUTPUT}" "${SOURCE}"
    RESULT_VARIABLE assemble_result
    OUTPUT_VARIABLE assemble_output
    ERROR_VARIABLE assemble_error
)
if(NOT assemble_result EQUAL 0)
    message(FATAL_ERROR
        "NASM failed for ${SOURCE}:\n"
        "${assemble_output}${assemble_error}"
    )
endif()
