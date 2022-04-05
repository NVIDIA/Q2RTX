SET(MEDIA_SOURCES
    ${SOURCE}/baseq2/env
    ${SOURCE}/baseq2/maps
    ${SOURCE}/baseq2/models
    ${SOURCE}/baseq2/overrides
    ${SOURCE}/baseq2/pics
    ${SOURCE}/baseq2/sound
    ${SOURCE}/baseq2/sprites
    ${SOURCE}/baseq2/textures
    ${SOURCE}/baseq2/materials
    ${SOURCE}/baseq2/prefetch.txt
    ${SOURCE}/baseq2/pt_toggles.cfg
    ${SOURCE}/baseq2/q2rtx.cfg
    ${SOURCE}/baseq2/q2rtx.menu
    ${SOURCE}/baseq2/sky_clusters.txt
)
set(out_file "${SOURCE}/baseq2/q2rtx_media.pkz")
exec_program(7za ARGS "a -tzip" ${out_file} ${MEDIA_SOURCES})
