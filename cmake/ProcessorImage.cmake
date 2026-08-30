include(CMakeParseArguments)

set(RP86_NASM_EXECUTABLE
    "${PROJECT_SOURCE_DIR}/.tools/nasm-3.02/bin/nasm"
    CACHE FILEPATH
    "Repository-local NASM 3.02 executable"
)

function(rp86_add_processor_image target_name)
    set(one_value_args SOURCE SYMBOL METADATA PACKAGE_NAME)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(IMAGE "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT IMAGE_SOURCE)
        message(FATAL_ERROR "rp86_add_processor_image(${target_name}) requires SOURCE")
    endif()
    if(NOT IMAGE_SYMBOL)
        set(IMAGE_SYMBOL "${target_name}")
    endif()

    get_filename_component(source_path "${IMAGE_SOURCE}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target_name}")
    set(binary_path "${generated_dir}/${target_name}.bin")
    set(generated_c "${generated_dir}/${target_name}.c")
    set(generated_h "${generated_dir}/${target_name}.h")

    add_custom_command(
        OUTPUT "${binary_path}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
        COMMAND "${CMAKE_COMMAND}"
            "-DNASM_EXECUTABLE=${RP86_NASM_EXECUTABLE}"
            "-DSOURCE=${source_path}"
            "-DOUTPUT=${binary_path}"
            -P "${PROJECT_SOURCE_DIR}/cmake/AssembleProcessorImage.cmake"
        DEPENDS
            "${source_path}"
            ${IMAGE_DEPENDS}
            "${PROJECT_SOURCE_DIR}/cmake/AssembleProcessorImage.cmake"
        VERBATIM
        COMMENT "Assembling processor image ${target_name}"
    )

    add_custom_command(
        OUTPUT "${generated_c}" "${generated_h}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${binary_path}"
            "-DOUTPUT_C=${generated_c}"
            "-DOUTPUT_H=${generated_h}"
            "-DSYMBOL=${IMAGE_SYMBOL}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        DEPENDS
            "${binary_path}"
            "${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake"
        VERBATIM
        COMMENT "Embedding processor image ${target_name}"
    )

    add_library(${target_name} STATIC EXCLUDE_FROM_ALL "${generated_c}")
    target_include_directories(${target_name} PUBLIC "${generated_dir}")

    add_custom_target(${target_name}_image
        DEPENDS "${binary_path}" "${generated_c}" "${generated_h}"
    )

    if(IMAGE_METADATA)
        if(NOT IMAGE_PACKAGE_NAME)
            set(IMAGE_PACKAGE_NAME "${target_name}.p86w")
        endif()
        get_filename_component(metadata_path "${IMAGE_METADATA}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(package_dir "${PROJECT_BINARY_DIR}/workloads")
        set(package_path "${package_dir}/${IMAGE_PACKAGE_NAME}")
        add_custom_command(
            OUTPUT "${package_path}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${package_dir}"
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/package_workload.py"
                --metadata "${metadata_path}"
                --image "${binary_path}"
                --output "${package_path}"
            DEPENDS
                "${binary_path}"
                "${metadata_path}"
                "${PROJECT_SOURCE_DIR}/tools/package_workload.py"
                "${PROJECT_SOURCE_DIR}/tools/rp86_runtime/workload.py"
            VERBATIM
            COMMENT "Packaging processor workload ${IMAGE_PACKAGE_NAME}"
        )
        add_custom_target(${target_name}_package ALL DEPENDS "${package_path}")
        set(${target_name}_PACKAGE "${package_path}" PARENT_SCOPE)
    endif()

    set(${target_name}_BINARY "${binary_path}" PARENT_SCOPE)
    set(${target_name}_HEADER "${generated_h}" PARENT_SCOPE)
endfunction()
