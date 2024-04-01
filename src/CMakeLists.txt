cmake_minimum_required (VERSION 3.9)
cmake_policy(SET CMP0069 NEW)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

SET(SRC_GAME
	game/g_ai.c
	game/g_chase.c
	game/g_cmds.c
	game/g_combat.c
	game/g_func.c
	game/g_items.c
	game/g_main.c
	game/g_misc.c
	game/g_monster.c
	game/g_phys.c
	game/g_ptrs.c
	game/g_ptrs_compat_v2.c
	game/g_save.c
	game/g_spawn.c
	game/g_svcmds.c
	game/g_target.c
	game/g_trigger.c
	game/g_turret.c
	game/g_utils.c
	game/g_weapon.c
	game/m_actor.c
	game/m_berserk.c
	game/m_boss2.c
	game/m_boss3.c
	game/m_boss31.c
	game/m_boss32.c
	game/m_brain.c
	game/m_chick.c
	game/m_flipper.c
	game/m_float.c
	game/m_flyer.c
	game/m_gladiator.c
	game/m_gunner.c
	game/m_hover.c
	game/m_infantry.c
	game/m_insane.c
	game/m_medic.c
	game/m_move.c
	game/m_mutant.c
	game/m_parasite.c
	game/m_soldier.c
	game/m_supertank.c
	game/m_tank.c
	game/p_client.c
	game/p_hud.c
	game/p_trail.c
	game/p_view.c
	game/p_weapon.c
)

SET(HEADERS_GAME
	game/g_local.h
	game/g_ptrs.h
	game/m_actor.h
	game/m_berserk.h
	game/m_boss2.h
	game/m_boss31.h
	game/m_boss32.h
	game/m_brain.h
	game/m_chick.h
	game/m_flipper.h
	game/m_float.h
	game/m_flyer.h
	game/m_gladiator.h
	game/m_gunner.h
	game/m_hover.h
	game/m_infantry.h
	game/m_insane.h
	game/m_medic.h
	game/m_mutant.h
	game/m_parasite.h
	game/m_player.h
	game/m_rider.h
	game/m_soldier.h
	game/m_supertank.h
	game/m_tank.h
)


SET(SRC_CLIENT
	client/ascii.c
	client/console.c
	client/cin.c
	client/crc.c
	client/demo.c
	client/download.c
	client/effects.c
	client/entities.c
	client/gtv.c
	client/input.c
	client/keys.c
	client/locs.c
	client/main.c
	client/newfx.c
#	client/null.c
	client/parse.c
	client/precache.c
	client/predict.c
	client/refresh.c
	client/screen.c
	client/tent.c
	client/view.c
	client/ui/demos.c
	client/ui/menu.c
	client/ui/playerconfig.c
	client/ui/playermodels.c
	client/ui/script.c
	client/ui/servers.c
	client/ui/ui.c
	client/sound/dma.c
	client/sound/al.c
	client/sound/main.c
	client/sound/mem.c
	client/sound/ogg.c
	client/sound/qal/fixed.c
#	client/sound/qal/dynamic.c
	common/async.c
)

SET(SRC_CLIENT_HTTP
	client/http.c
)

SET(HEADERS_CLIENT
	client/client.h
	client/ui/ui.h
	client/sound/sound.h
	client/sound/qal/dynamic.h
	client/sound/qal/fixed.h
)

SET(SRC_SERVER
	server/commands.c
	server/entities.c
	server/game.c
	server/init.c
	server/main.c
	server/mvd.c
	server/send.c
	server/user.c
	server/world.c
	server/mvd/client.c
	server/mvd/parse.c
	server/mvd/game.c
	server/save.c
)

SET(HEADERS_SERVER
	server/server.h
)

SET(SRC_COMMON
	common/bsp.c
	common/cmd.c
	common/cmodel.c
	common/common.c
	common/cvar.c
	common/error.c
	common/field.c
	common/fifo.c
	common/files.c
	common/math.c
	common/mdfour.c
	common/msg.c
	common/pmove.c
	common/prompt.c
	common/sizebuf.c
#	common/tests.c
	common/utils.c
	common/zone.c
	common/net/chan.c
	common/net/net.c
)

SET(HEADERS_COMMON
	common/net/win.h
)

SET(SRC_REFRESH
	refresh/images.c
	refresh/models.c
	refresh/model_iqm.c
	refresh/stb/stb.c
)


SET(SRC_GL
	refresh/gl/draw.c
	refresh/gl/hq2x.c
	refresh/gl/texture.c
	refresh/gl/main.c
	refresh/gl/mesh.c
	refresh/gl/models.c
	refresh/gl/sky.c
	refresh/gl/state.c
	refresh/gl/surf.c
	refresh/gl/tess.c
	refresh/gl/world.c
	refresh/gl/qgl.c
	refresh/gl/legacy.c
	refresh/gl/shader.c
)

SET(HEADERS_GL
	refresh/gl/arbfp.h
	refresh/gl/gl.h
)

SET(SRC_SHARED
	shared/m_flash.c
	shared/shared.c
)

SET(SRC_LINUX
	unix/hunk.c
	unix/system.c
	unix/tty.c
)

SET(SRC_LINUX_CLIENT
	unix/sound/sdl.c
	unix/video/sdl.c
)

SET(SRC_WINDOWS
	windows/ac.c
	windows/debug.c
	windows/hunk.c
	windows/pthread.c
	windows/system.c
)

SET(SRC_WINDOWS_CLIENT
	windows/wave.c
	unix/sound/sdl.c
	unix/video/sdl.c
)

	#windows/dinput.c
	#windows/swimp.c
	#windows/wgl.c
	#unix/sdl2/sound.c

SET(HEADERS_WINDOWS
	windows/client.h
	windows/threads/threads.h
)

SET(SRC_VKPT
	refresh/vkpt/asvgf.c
	refresh/vkpt/bloom.c
	refresh/vkpt/bsp_mesh.c
	refresh/vkpt/draw.c
	refresh/vkpt/fog.c
	refresh/vkpt/cameras.c
	refresh/vkpt/freecam.c
	refresh/vkpt/fsr.c
	refresh/vkpt/main.c
	refresh/vkpt/material.c
	refresh/vkpt/matrix.c
	refresh/vkpt/mgpu.c
	refresh/vkpt/models.c
	refresh/vkpt/path_tracer.c
	refresh/vkpt/physical_sky.c
	refresh/vkpt/precomputed_sky.c
	refresh/vkpt/profiler.c
	refresh/vkpt/shadow_map.c
	refresh/vkpt/textures.c
	refresh/vkpt/tone_mapping.c
	refresh/vkpt/transparency.c
	refresh/vkpt/uniform_buffer.c
	refresh/vkpt/vertex_buffer.c
	refresh/vkpt/vk_util.c
	refresh/vkpt/buddy_allocator.c
	refresh/vkpt/device_memory_allocator.c
	refresh/vkpt/god_rays.c
	refresh/vkpt/conversion.c
)

SET(HEADERS_VKPT
	refresh/vkpt/vkpt.h
	refresh/vkpt/vk_util.h
	refresh/vkpt/buddy_allocator.h
	refresh/vkpt/device_memory_allocator.h
	refresh/vkpt/fog.h
	refresh/vkpt/cameras.h
	refresh/vkpt/material.h
	refresh/vkpt/physical_sky.h
	refresh/vkpt/precomputed_sky.h
	refresh/vkpt/conversion.h
)

set(SRC_SHADERS
	refresh/vkpt/shader/animate_materials.comp
	refresh/vkpt/shader/god_rays_filter.comp
	refresh/vkpt/shader/god_rays.comp
	refresh/vkpt/shader/bloom_composite.comp
	refresh/vkpt/shader/bloom_blur.comp
	refresh/vkpt/shader/bloom_downscale.comp
	refresh/vkpt/shader/compositing.comp
	refresh/vkpt/shader/checkerboard_interleave.comp
	refresh/vkpt/shader/asvgf_atrous.comp
	refresh/vkpt/shader/asvgf_gradient_atrous.comp
	refresh/vkpt/shader/asvgf_gradient_img.comp
	refresh/vkpt/shader/asvgf_gradient_reproject.comp
	refresh/vkpt/shader/asvgf_lf.comp
	refresh/vkpt/shader/asvgf_taau.comp
	refresh/vkpt/shader/asvgf_temporal.comp
	refresh/vkpt/shader/instance_geometry.comp
	refresh/vkpt/shader/normalize_normal_map.comp
	refresh/vkpt/shader/tone_mapping_histogram.comp
	refresh/vkpt/shader/tone_mapping_curve.comp
    refresh/vkpt/shader/tone_mapping_apply.comp
	refresh/vkpt/shader/physical_sky.comp
	refresh/vkpt/shader/physical_sky_space.comp
	refresh/vkpt/shader/shadow_map.vert
	refresh/vkpt/shader/sky_buffer_resolve.comp
	refresh/vkpt/shader/stretch_pic.frag
	refresh/vkpt/shader/stretch_pic.vert
	refresh/vkpt/shader/final_blit.frag
	refresh/vkpt/shader/final_blit.vert
	refresh/vkpt/shader/fsr_easu_fp16.comp
	refresh/vkpt/shader/fsr_easu_fp32.comp
	refresh/vkpt/shader/fsr_rcas_fp16.comp
	refresh/vkpt/shader/fsr_rcas_fp32.comp
)

set(SRC_RT_SHADERS
	refresh/vkpt/shader/primary_rays.rgen
	refresh/vkpt/shader/direct_lighting.rgen
	refresh/vkpt/shader/indirect_lighting.rgen
	refresh/vkpt/shader/path_tracer.rchit
	refresh/vkpt/shader/path_tracer.rmiss
	refresh/vkpt/shader/path_tracer_masked.rahit
	refresh/vkpt/shader/path_tracer_particle.rahit
	refresh/vkpt/shader/path_tracer_sprite.rahit
	refresh/vkpt/shader/path_tracer_beam.rahit
	refresh/vkpt/shader/path_tracer_beam.rint
	refresh/vkpt/shader/path_tracer_explosion.rahit
	refresh/vkpt/shader/reflect_refract.rgen
)

# Only build shaders in 64-bit builds, because the client
# is also only built for 64-bit systems.
IF(IS_64_BIT)
    include(../cmake/compileShaders.cmake)

    foreach(s ${SRC_RT_SHADERS})
        get_filename_component(shader_name ${s} NAME)
        get_filename_component(shader_ext ${s} EXT)
        compile_shader(SOURCE_FILE ${s} OUTPUT_FILE_LIST shader_bytecode OUTPUT_FILE_NAME "${shader_name}.pipeline")
        if (shader_ext STREQUAL ".rgen")
            compile_shader(SOURCE_FILE ${s} OUTPUT_FILE_LIST shader_bytecode OUTPUT_FILE_NAME "${shader_name}.query" DEFINES "-DKHR_RAY_QUERY" STAGE "comp")
        endif()
    endforeach()

    foreach(s ${SRC_SHADERS})
        compile_shader(SOURCE_FILE ${s} INCLUDES "-I${CMAKE_CURRENT_SOURCE_DIR}/refresh/vkpt/fsr" OUTPUT_FILE_LIST shader_bytecode)
    endforeach()

    add_custom_target(shaders DEPENDS ${shader_bytecode})

    if(TARGET glslangValidator)
        add_dependencies(shaders glslangValidator)
    endif()
ENDIF()

# Embed version number
ADD_DEFINITIONS(-DVERSION_MAJOR=${Q2RTX_VERSION_MAJOR})
ADD_DEFINITIONS(-DVERSION_MINOR=${Q2RTX_VERSION_MINOR})
ADD_DEFINITIONS(-DVERSION_POINT=${Q2RTX_VERSION_POINT})
ADD_DEFINITIONS(-DVERSION_SHA=${Q2RTX_VERSION_SHA})
ADD_DEFINITIONS(-DVERSION_BRANCH=${Q2RTX_VERSION_BRANCH})

ADD_DEFINITIONS(-DHAVE_CONFIG_H=1 -DCURL_STATICLIB)

IF (NOT USE_SYSTEM_OPENAL)
    ADD_DEFINITIONS(-DAL_LIBTYPE_STATIC)
ENDIF()

IF(WIN32)
	ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
ENDIF()

add_compile_definitions($<$<CONFIG:Debug>:USE_DEBUG>)

if(NOT WIN32)
    add_compile_definitions(_GNU_SOURCE)
endif()

# Check whether compiler supports 'flagstr', and if so, set ${flagvar} to that value
function(CCompilerFlagString flagvar flagstr)
    CHECK_C_COMPILER_FLAG("${flagstr}" ${flagvar}_FOUND)
    if(${flagvar}_FOUND)
        set(${flagvar} "${flagstr}" PARENT_SCOPE)
    endif()
endfunction()
# Check whether compiler supports 'flagstr', and if so, append that value to ${flagvar}
function(CCompilerFlagStringAppend flagvar flagstr)
    string(TOUPPER "${flagstr}" auto_flag_var)
    string(REGEX REPLACE "[^A-Z0-9]" "_" auto_flag_var "${auto_flag_var}")
    CCompilerFlagString (C_${auto_flag_var} "${flagstr}")
    if(NOT "${C_${auto_flag_var}}" EQUAL "")
        set(${flagvar} "${${flagvar}} ${C_${auto_flag_var}} " PARENT_SCOPE)
    endif()
endfunction()

if(NOT MSVC)
    CCompilerFlagStringAppend(CMAKE_C_FLAGS "-fsigned-char")
    CCompilerFlagStringAppend(CMAKE_C_FLAGS "-Wall")
    CCompilerFlagStringAppend(CMAKE_C_FLAGS "-Wpointer-arith")
    CCompilerFlagStringAppend(CMAKE_C_FLAGS "-Wformat-security")
    CCompilerFlagStringAppend(CMAKE_C_FLAGS "-Werror=vla")
    CCompilerFlagStringAppend(CMAKE_C_FLAGS "-Wstrict-prototypes")
    # CCompilerFlagString(WARN_MISSING_PROTOTYPES "-Wmissing-prototypes")
endif()

ADD_LIBRARY(game SHARED ${SRC_GAME} ${HEADERS_GAME} ${SRC_SHARED})
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    # Workaround for GCC 10 linking shared libgcc by default
    target_link_options(game PRIVATE "-static-libgcc")
endif()

IF(WIN32)
    IF(IS_64_BIT)
        ADD_EXECUTABLE(client WIN32 
            ${SRC_CLIENT} ${HEADERS_CLIENT} 
            ${SRC_COMMON} ${HEADERS_COMMON} 
            ${SRC_REFRESH} ${SRC_SHADERS} 
            ${SRC_SHARED} 
            ${SRC_WINDOWS} ${SRC_WINDOWS_CLIENT} ${HEADERS_WINDOWS}
            ${SRC_SERVER} ${HEADERS_SERVER}
            windows/res/q2rtx.rc
        )
    ENDIF()
    ADD_EXECUTABLE(server 
        ${SRC_COMMON} ${HEADERS_COMMON} 
        ${SRC_SHARED} 
        ${SRC_WINDOWS} ${HEADERS_WINDOWS}
        ${SRC_SERVER} ${HEADERS_SERVER}
        server/ac.c
        client/null.c
        windows/res/q2rtxded.rc
    )
ELSE()
    IF(IS_64_BIT)
        ADD_EXECUTABLE(client
            ${SRC_CLIENT} ${HEADERS_CLIENT} 
            ${SRC_COMMON} ${HEADERS_COMMON} 
            ${SRC_REFRESH} ${SRC_SHADERS} 
            ${SRC_SHARED} 
            ${SRC_LINUX} ${SRC_LINUX_CLIENT}
            ${SRC_SERVER} ${HEADERS_SERVER}
        )
    ENDIF()
    ADD_EXECUTABLE(server
        ${SRC_COMMON} ${HEADERS_COMMON} 
        ${SRC_SHARED} 
        ${SRC_LINUX}
        ${SRC_SERVER} ${HEADERS_SERVER}
        server/ac.c
        client/null.c
    )
ENDIF()

TARGET_COMPILE_DEFINITIONS(server PRIVATE USE_SERVER=1 USE_SAVEGAMES=1)
target_compile_options(server PRIVATE "${WARN_MISSING_PROTOTYPES}")

IF (TARGET client)
    TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_CLIENT=1 USE_SAVEGAMES=1 USE_FIXED_LIBAL=1 USE_SDL=1)

    IF(CONFIG_USE_CURL)
        TARGET_SOURCES(client PRIVATE ${SRC_CLIENT_HTTP})
        TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_CURL=1)
        TARGET_LINK_LIBRARIES(client libcurl)
    ENDIF()

    add_dependencies(client shaders)

    IF (CONFIG_GL_RENDERER)
        TARGET_SOURCES(client PRIVATE ${SRC_GL} ${HEADERS_GL})
        TARGET_COMPILE_DEFINITIONS(client PRIVATE REF_GL=1 USE_REF=1)
    ENDIF()

    IF (CONFIG_VKPT_RENDERER)
        TARGET_SOURCES(client PRIVATE ${SRC_VKPT} ${HEADERS_VKPT})
        TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${CMAKE_SOURCE_DIR}/extern/Vulkan-Headers/include)
        LINK_DIRECTORIES(client PRIVATE ${CMAKE_SOURCE_DIR})
        TARGET_COMPILE_DEFINITIONS(client PRIVATE REF_VKPT=1 USE_REF=1)
        IF (CONFIG_VKPT_ENABLE_DEVICE_GROUPS)
            TARGET_COMPILE_DEFINITIONS(client PRIVATE VKPT_DEVICE_GROUPS)
        ENDIF()
        if (CONFIG_VKPT_ENABLE_IMAGE_DUMPS)
            TARGET_COMPILE_DEFINITIONS(client PRIVATE VKPT_IMAGE_DUMPS)
        ENDIF()
        IF (WIN32)
        TARGET_LINK_LIBRARIES(client vulkan-1)
        ELSE ()
        TARGET_LINK_LIBRARIES(client vulkan)
        ENDIF()
    ENDIF()

    TARGET_LINK_LIBRARIES(client stb)
    TARGET_LINK_LIBRARIES(client tinyobjloader)
    TARGET_LINK_LIBRARIES(client OpenAL)
ENDIF()

SOURCE_GROUP("game\\sources" FILES ${SRC_GAME})
SOURCE_GROUP("game\\headers" FILES ${HEADERS_GAME})
SOURCE_GROUP("client\\sources" FILES ${SRC_CLIENT})
SOURCE_GROUP("client\\headers" FILES ${HEADERS_CLIENT})
SOURCE_GROUP("server\\sources" FILES ${SRC_SERVER})
SOURCE_GROUP("server\\headers" FILES ${HEADERS_SERVER})
SOURCE_GROUP("common\\sources" FILES ${SRC_COMMON})
SOURCE_GROUP("common\\headers" FILES ${HEADERS_COMMON})
SOURCE_GROUP("refresh\\sources" FILES ${SRC_REFRESH} ${SRC_VKPT})
SOURCE_GROUP("refresh\\headers" FILES ${HEADERS_VKPT})
SOURCE_GROUP("refresh\\shaders" FILES ${SRC_SHADERS})
SOURCE_GROUP("shared\\sources" FILES ${SRC_SHARED})
SOURCE_GROUP("windows\\sources" FILES ${SRC_WINDOWS})
SOURCE_GROUP("windows\\headers" FILES ${HEADERS_WINDOWS})

IF (WIN32)
    IF(TARGET client)
        TARGET_INCLUDE_DIRECTORIES(client PRIVATE ../VC/inc)
        TARGET_LINK_LIBRARIES(client winmm ws2_32)
        SET_TARGET_PROPERTIES(client PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        # macro redefinition, deprecation
        TARGET_COMPILE_OPTIONS(client PRIVATE /wd4005 /wd4996)
    ENDIF()

    TARGET_INCLUDE_DIRECTORIES(server PRIVATE ../VC/inc)
    TARGET_INCLUDE_DIRECTORIES(game PRIVATE ../VC/inc)
    TARGET_SOURCES(game PRIVATE windows/res/game.rc)

    TARGET_LINK_LIBRARIES(server winmm ws2_32)

    SET_TARGET_PROPERTIES(server PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # macro redefinition, deprecation
    TARGET_COMPILE_OPTIONS(server PRIVATE /wd4005 /wd4996)
    TARGET_COMPILE_OPTIONS(game PRIVATE /wd4005 /wd4996)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(game PRIVATE ../inc)

IF(TARGET client)
    TARGET_INCLUDE_DIRECTORIES(client PRIVATE ../inc)
    IF(NOT USE_SYSTEM_ZLIB)
      TARGET_INCLUDE_DIRECTORIES(client PRIVATE "${ZLIB_INCLUDE_DIRS}")
    ENDIF()
ENDIF()

TARGET_INCLUDE_DIRECTORIES(server PRIVATE ../inc)

if(NOT USE_SYSTEM_ZLIB)
    TARGET_INCLUDE_DIRECTORIES(server PRIVATE "${ZLIB_INCLUDE_DIRS}")
endif()

if(TARGET client)
    TARGET_LINK_LIBRARIES(client SDL2main SDL2-static)
endif()

if(USE_SYSTEM_ZLIB)
    TARGET_LINK_LIBRARIES(server z)
    TARGET_LINK_LIBRARIES(game z)
    if(TARGET client)
        TARGET_LINK_LIBRARIES(client z)
    endif()
else()
    TARGET_LINK_LIBRARIES(server zlibstatic)
    TARGET_LINK_LIBRARIES(game zlibstatic)
    if(TARGET client)
        TARGET_LINK_LIBRARIES(client zlibstatic)
    endif()
endif()

IF(UNIX)
    TARGET_LINK_LIBRARIES(server dl rt m pthread)
ENDIF()

IF(TARGET client)
    SET_TARGET_PROPERTIES(client
        PROPERTIES
        OUTPUT_NAME "q2rtx"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}"
        DEBUG_POSTFIX ""
    )
ENDIF()

SET_TARGET_PROPERTIES(server
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}"
    DEBUG_POSTFIX ""
)

IF (IS_64_BIT)
    SET_TARGET_PROPERTIES(server PROPERTIES OUTPUT_NAME "q2rtxded")
ELSE()
    SET_TARGET_PROPERTIES(server PROPERTIES OUTPUT_NAME "q2rtxded-x86")
ENDIF()

# specify both LIBRARY and RUNTIME because one works only on Windows and another works only on Linux

SET_TARGET_PROPERTIES(game
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/baseq2"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/baseq2"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/baseq2"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/baseq2"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/baseq2"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/baseq2"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/baseq2"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/baseq2"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/baseq2"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/baseq2"
    PREFIX ""
    DEBUG_POSTFIX ""
)

IF(IS_64_BIT)
	IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
		SET_TARGET_PROPERTIES(game
			PROPERTIES
			LIBRARY_OUTPUT_NAME "gameaarch64"
			RUNTIME_OUTPUT_NAME "gameaarch64"
		)
	ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
		SET_TARGET_PROPERTIES(game
			PROPERTIES
			LIBRARY_OUTPUT_NAME "gameppc64le"
			RUNTIME_OUTPUT_NAME "gameppc64le"
		)
	ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "e2k")
		SET_TARGET_PROPERTIES(game
			PROPERTIES
			LIBRARY_OUTPUT_NAME "gamee2k"
			RUNTIME_OUTPUT_NAME "gamee2k"
		)
	ELSE()
		SET_TARGET_PROPERTIES(game
			PROPERTIES
			LIBRARY_OUTPUT_NAME "gamex86_64"
			RUNTIME_OUTPUT_NAME "gamex86_64"
		)
	ENDIF()
ELSE()
    SET_TARGET_PROPERTIES(game
        PROPERTIES
        LIBRARY_OUTPUT_NAME "gamex86"
        RUNTIME_OUTPUT_NAME "gamex86"
    )
ENDIF()

IF(CONFIG_LINUX_PACKAGING_SUPPORT)
    # Put the real game binary in /usr/share so we can have a wrapper in /usr/bin
    IF(TARGET client)
        INSTALL(TARGETS client DESTINATION share/quake2rtx/bin COMPONENT shareware)
    ENDIF()
    INSTALL(TARGETS server DESTINATION games COMPONENT shareware)

    IF(NOT CONFIG_LINUX_PACKAGING_SKIP_PKZ)
	    # Compress NVIDIA custom content
	    INSTALL(CODE "set(SOURCE \"${CMAKE_SOURCE_DIR}\")"
	        SCRIPT "${CMAKE_SOURCE_DIR}/setup/package_media.cmake"
	        SCRIPT "${CMAKE_SOURCE_DIR}/setup/package_shaders.cmake"
	        COMPONENT shareware)
	ENDIF()
    INSTALL(TARGETS game DESTINATION share/quake2rtx/baseq2 COMPONENT shareware)

    # Package data files, including the shareware pak0 for demo levels
    set (SHAREWARE_DATA_FILES_LIST
        "${CMAKE_SOURCE_DIR}/baseq2/q2rtx_media.pkz"
        "${CMAKE_SOURCE_DIR}/baseq2/shaders.pkz"
        "${CMAKE_SOURCE_DIR}/baseq2/blue_noise.pkz"
        "${CMAKE_SOURCE_DIR}/baseq2/shareware/pak0.pak"
    )
    INSTALL(FILES
        ${SHAREWARE_DATA_FILES_LIST}
        DESTINATION share/quake2rtx/baseq2
        COMPONENT shareware)

    # Package shareware player model
    INSTALL(DIRECTORY
        "${CMAKE_SOURCE_DIR}/baseq2/shareware/players"
        DESTINATION share/quake2rtx/baseq2
        COMPONENT shareware)

    # Package the legal and info files
    INSTALL(FILES
        "${CMAKE_SOURCE_DIR}/license.txt"
        "${CMAKE_SOURCE_DIR}/notice.txt"
        "${CMAKE_SOURCE_DIR}/readme.md"
        "${CMAKE_SOURCE_DIR}/changelog.md"
        DESTINATION share/quake2rtx/
        COMPONENT shareware)

    # Package the icon/desktop file
    INSTALL(FILES
        "${CMAKE_SOURCE_DIR}/setup/q2rtx.desktop"
        DESTINATION share/applications/
        COMPONENT shareware)
    INSTALL(FILES
        "${CMAKE_SOURCE_DIR}/setup/q2rtx.png"
        DESTINATION share/pixmaps/
        COMPONENT shareware)

    # Install Client Wrapper
    INSTALL(FILES
        "${CMAKE_SOURCE_DIR}/setup/q2rtx.sh"
        DESTINATION games/
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        COMPONENT shareware
	RENAME q2rtx)

    # Install PAK file finder
    INSTALL(FILES
        "${CMAKE_SOURCE_DIR}/setup/find-retail-paks.sh"
        DESTINATION share/quake2rtx/bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        COMPONENT shareware)
ENDIF()
