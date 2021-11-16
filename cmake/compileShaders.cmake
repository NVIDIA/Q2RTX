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
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/path_tracer_hit_shaders.h
    ${CMAKE_SOURCE_DIR}/src/refresh/vkpt/shader/path_tracer_transparency.glsl
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

if(TARGET glslangValidator)
    set(GLSLANG_COMPILER "$<TARGET_FILE:glslangValidator>")
    message(STATUS "Using glslangValidator built from source")
else()
    find_program(GLSLANG_COMPILER glslangValidator PATHS "$ENV{VULKAN_SDK}/bin/")

    if(NOT GLSLANG_COMPILER)
        message(FATAL_ERROR "Couldn't find glslangValidator! "
            "Please provide a valid path to it using the GLSLANG_COMPILER variable.")
    endif()
    
    message(STATUS "Using this glslangValidator: ${GLSLANG_COMPILER}")
endif()

function(compile_shader)
    set(options "")
    set(oneValueArgs SOURCE_FILE OUTPUT_FILE_NAME OUTPUT_FILE_LIST STAGE)
    set(multiValueArgs DEFINES INCLUDES)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_SOURCE_FILE)
        message(FATAL_ERROR "compile_shader: SOURCE_FILE argument missing")
    endif()

    if (NOT params_OUTPUT_FILE_LIST)
        message(FATAL_ERROR "compile_shader: OUTPUT_FILE_LIST argument missing")
    endif()

    set(src_file "${CMAKE_CURRENT_SOURCE_DIR}/${params_SOURCE_FILE}")

    if (params_OUTPUT_FILE_NAME)
        set(output_file_name ${params_OUTPUT_FILE_NAME})
    else()
        get_filename_component(output_file_name ${src_file} NAME)
    endif()

    if (params_STAGE)
        set(stage -S comp)
    else()
        set(stage)
    endif()
    
    set_source_files_properties(${src_file} PROPERTIES VS_TOOL_OVERRIDE "None")

    set (out_dir "${CMAKE_SOURCE_DIR}/baseq2/shader_vkpt")
    set (out_file "${out_dir}/${output_file_name}.spv")
    
    set(glslang_command_line
            ${stage}
            --target-env vulkan1.2
            --quiet
            -DVKPT_SHADER
            -V
            ${params_DEFINES}
            ${params_INCLUDES}
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
