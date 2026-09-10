include(CMakeParseArguments)

set(RP86_WCC_EXECUTABLE
    "wcc"
    CACHE FILEPATH
    "Open Watcom 16-bit C compiler executable"
)
set(RP86_WLINK_EXECUTABLE
    "wlink"
    CACHE FILEPATH
    "Open Watcom linker executable"
)
set(RP86_C16_MEMORY_MODEL
    "-ms"
    CACHE STRING
    "Open Watcom 16-bit C memory-model switch used by RP86 C16 workloads"
)

function(rp86_add_processor_c16_image target_name)
    set(one_value_args METADATA PACKAGE_NAME LOAD_ADDRESS)
    set(multi_value_args C_SOURCES ASM_SOURCES INCLUDE_DIRS C_OPTIONS ASM_OPTIONS DEPENDS)
    cmake_parse_arguments(C16 "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT C16_C_SOURCES)
        message(FATAL_ERROR "rp86_add_processor_c16_image(${target_name}) requires C_SOURCES")
    endif()
    if(NOT C16_ASM_SOURCES)
        message(FATAL_ERROR "rp86_add_processor_c16_image(${target_name}) requires ASM_SOURCES")
    endif()
    if(NOT C16_LOAD_ADDRESS)
        set(C16_LOAD_ADDRESS "0x10000")
    endif()

    if(NOT DEFINED RP86_NASM_EXECUTABLE OR RP86_NASM_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "RP86_NASM_EXECUTABLE must be defined before using ${target_name}; "
            "include ProcessorImage.cmake first"
        )
    endif()

    set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target_name}")
    set(binary_path "${generated_dir}/${target_name}.bin")
    set(map_path "${generated_dir}/${target_name}.map")
    set(link_script "${generated_dir}/${target_name}.lnk")
    set(object_paths "")

    set(wcc_include_options "")
    foreach(include_dir IN LISTS C16_INCLUDE_DIRS)
        get_filename_component(include_path "${include_dir}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND wcc_include_options "-i=${include_path}")
    endforeach()

    set(source_index 0)
    foreach(c_source IN LISTS C16_C_SOURCES)
        math(EXPR source_index "${source_index} + 1")
        get_filename_component(source_path "${c_source}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(source_stem "${c_source}" NAME_WE)
        set(object_path "${generated_dir}/c${source_index}_${source_stem}.obj")
        list(APPEND object_paths "${object_path}")

        add_custom_command(
            OUTPUT "${object_path}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
            COMMAND "${RP86_WCC_EXECUTABLE}"
                -0
                "${RP86_C16_MEMORY_MODEL}"
                -zu
                -s
                -zl
                -zq
                -bt=dos
                ${wcc_include_options}
                ${C16_C_OPTIONS}
                "-fo=${object_path}"
                "${source_path}"
            DEPENDS "${source_path}" ${C16_DEPENDS}
            VERBATIM
            COMMENT "Compiling 8086 C/16 object ${source_stem}"
        )
    endforeach()

    set(source_index 0)
    foreach(asm_source IN LISTS C16_ASM_SOURCES)
        math(EXPR source_index "${source_index} + 1")
        get_filename_component(source_path "${asm_source}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(source_stem "${asm_source}" NAME_WE)
        set(object_path "${generated_dir}/a${source_index}_${source_stem}.obj")

        # Put assembly objects before C objects in the WLINK file list.  The
        # initial RP86 startup stub intentionally occupies offset zero of the
        # combined _TEXT segment.
        list(PREPEND object_paths "${object_path}")

        add_custom_command(
            OUTPUT "${object_path}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
            COMMAND "${RP86_NASM_EXECUTABLE}"
                -f obj
                -o "${object_path}"
                "-I${PROJECT_SOURCE_DIR}/processor/include/"
                ${C16_ASM_OPTIONS}
                "${source_path}"
            DEPENDS
                "${source_path}"
                ${C16_DEPENDS}
                "${PROJECT_SOURCE_DIR}/processor/include/rp86_abi.inc"
            VERBATIM
            COMMENT "Assembling 8086 OMF object ${source_stem}"
        )
    endforeach()

    add_custom_command(
        OUTPUT "${binary_path}" "${map_path}"
        COMMAND "${CMAKE_COMMAND}"
            "-DWLINK_EXECUTABLE=${RP86_WLINK_EXECUTABLE}"
            "-DOUTPUT=${binary_path}"
            "-DMAP=${map_path}"
            "-DLINK_SCRIPT=${link_script}"
            "-DLOAD_ADDRESS=${C16_LOAD_ADDRESS}"
            "-DOBJECTS=${object_paths}"
            -P "${PROJECT_SOURCE_DIR}/cmake/LinkProcessorC16.cmake"
        DEPENDS
            ${object_paths}
            "${PROJECT_SOURCE_DIR}/cmake/LinkProcessorC16.cmake"
        VERBATIM
        COMMENT "Linking raw 8086 C/16 image ${target_name}"
    )

    add_custom_target(${target_name}_image
        DEPENDS "${binary_path}" "${map_path}"
    )

    if(C16_METADATA)
        if(NOT C16_PACKAGE_NAME)
            set(C16_PACKAGE_NAME "${target_name}.p86w")
        endif()
        get_filename_component(metadata_path "${C16_METADATA}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(package_dir "${PROJECT_BINARY_DIR}/workloads")
        set(package_path "${package_dir}/${C16_PACKAGE_NAME}")

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
            COMMENT "Packaging processor C/16 workload ${C16_PACKAGE_NAME}"
        )
        add_custom_target(${target_name}_package DEPENDS "${package_path}")
        set(${target_name}_PACKAGE "${package_path}" PARENT_SCOPE)
    endif()

    set(${target_name}_BINARY "${binary_path}" PARENT_SCOPE)
    set(${target_name}_MAP "${map_path}" PARENT_SCOPE)
endfunction()
