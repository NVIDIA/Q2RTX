SET(SHADER_SOURCES
    ${SOURCE}/baseq2/shader_vkpt
)
set(out_file "${SOURCE}/baseq2/shaders.pkz")
exec_program(7za ARGS "a -tzip" ${out_file} ${SHADER_SOURCES})
