include(CMakeParseArguments)

set(PI86_NASM_EXECUTABLE
    "${PROJECT_SOURCE_DIR}/.tools/nasm-3.02/bin/nasm"
    CACHE FILEPATH
    "Repository-local NASM 3.02 executable"
)

function(pi86_add_v30_rom target_name)
    set(one_value_args SOURCE SYMBOL)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(ROM "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT ROM_SOURCE)
        message(FATAL_ERROR "pi86_add_v30_rom(${target_name}) requires SOURCE")
    endif()
    if(NOT ROM_SYMBOL)
        set(ROM_SYMBOL "${target_name}")
    endif()

    get_filename_component(source_path "${ROM_SOURCE}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target_name}")
    set(binary_path "${generated_dir}/${target_name}.bin")
    set(generated_c "${generated_dir}/${target_name}.c")
    set(generated_h "${generated_dir}/${target_name}.h")

    add_custom_command(
        OUTPUT "${binary_path}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
        COMMAND "${CMAKE_COMMAND}"
            "-DNASM_EXECUTABLE=${PI86_NASM_EXECUTABLE}"
            "-DSOURCE=${source_path}"
            "-DOUTPUT=${binary_path}"
            -P "${PROJECT_SOURCE_DIR}/cmake/AssembleV30Rom.cmake"
        DEPENDS
            "${source_path}"
            ${ROM_DEPENDS}
            "${PROJECT_SOURCE_DIR}/cmake/AssembleV30Rom.cmake"
        VERBATIM
        COMMENT "Assembling V30 ROM ${target_name}"
    )

    add_custom_command(
        OUTPUT "${generated_c}" "${generated_h}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${binary_path}"
            "-DOUTPUT_C=${generated_c}"
            "-DOUTPUT_H=${generated_h}"
            "-DSYMBOL=${ROM_SYMBOL}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        DEPENDS
            "${binary_path}"
            "${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        VERBATIM
        COMMENT "Embedding V30 ROM ${target_name}"
    )

    add_library(${target_name} STATIC EXCLUDE_FROM_ALL "${generated_c}")
    target_include_directories(${target_name} PUBLIC "${generated_dir}")

    add_custom_target(${target_name}_image
        DEPENDS "${binary_path}" "${generated_c}" "${generated_h}"
    )

    set(${target_name}_BINARY "${binary_path}" PARENT_SCOPE)
    set(${target_name}_HEADER "${generated_h}" PARENT_SCOPE)
endfunction()
