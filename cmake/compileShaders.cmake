set(SHADER_SOURCE_DEPENDENCIES
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/asvgf.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/brdf.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/constants.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/global_textures.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/global_ubo.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/god_rays_shared.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/light_lists.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/path_tracer_rgen.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/path_tracer.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/precomputed_sky.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/precomputed_sky_params.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/projection.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/read_visbuf.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/sky.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/tiny_encryption_algorithm.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/tone_mapping_utils.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/utils.glsl
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/vertex_buffer.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/water.glsl)

find_program(GLSLANG_COMPILER
        glslangValidator
    PATHS
        $ENV{VULKAN_SDK}/bin/
)

message(STATUS "Glslang compiler : ${GLSLANG_COMPILER}")

function(compile_shader)
    set(options "")
    set(oneValueArgs SOURCE_FILE OUTPUT_FILE_LIST)
    set(multiValueArgs "")
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_SOURCE_FILE)
        message(FATAL_ERROR "compile_shader: SOURCE_FILE argument missing")
    endif()

    if (NOT params_OUTPUT_FILE_LIST)
        message(FATAL_ERROR "compile_shader: OUTPUT_FILE_LIST argument missing")
    endif()

    set(src_file "${CMAKE_CURRENT_SOURCE_DIR}/${params_SOURCE_FILE}")
    get_filename_component(base_file_name ${src_file} NAME)
    get_filename_component(file_extension ${src_file} EXT)

    if (file_extension STREQUAL ".comp")
        set(DEFINES "-DSHADER_STAGE_COMP")
    elseif(file_extension STREQUAL ".rahit")
        set(DEFINES "-DSHADER_STAGE_ACHIT")
    elseif(file_extension STREQUAL ".rmiss")
        set(DEFINES "-DSHADER_STAGE_RMISS")
    elseif(file_extension STREQUAL ".rchit")
        set(DEFINES "-DSHADER_STAGE_RCHIT")
    elseif(file_extension STREQUAL ".rgen")
        set(DEFINES "-DSHADER_STAGE_RGEN")
    elseif(file_extension STREQUAL ".frag")
        set(DEFINES "-DSHADER_STAGE_FRAG")
    elseif(file_extension STREQUAL ".vert")
        set(DEFINES "-DSHADER_STAGE_VERT")
    else()
        message(FATAL_ERROR "unknown extension in shader source file: ${file_extension}")
    endif()

    set_source_files_properties(${src_file} PROPERTIES VS_TOOL_OVERRIDE "None")

    set (out_dir "${CMAKE_SOURCE_DIR}/baseq2/shader_vkpt")
    set (out_file "${out_dir}/${base_file_name}.spv")
    
    set(glslang_command_line
            --target-env vulkan1.1
            -DVKPT_SHADER
            -V
            ${DEFINES}
            "${src_file}"
            -o "${out_file}")

    add_custom_command(OUTPUT ${out_file}
                       DEPENDS ${src_file}
                       DEPENDS ${SHADER_SOURCE_DEPENDENCIES}
                       MAIN_DEPENDENCY ${src_file}
                       COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
                       COMMAND ${GLSLANG_COMPILER} ${glslang_command_line})
    
    set(${params_OUTPUT_FILE_LIST} ${${params_OUTPUT_FILE_LIST}} ${out_file} PARENT_SCOPE)
endfunction()
