cmake_minimum_required (VERSION 3.8)

SET(SRC_BASEQ2
	baseq2/g_ai.c
	baseq2/g_chase.c
	baseq2/g_cmds.c
	baseq2/g_combat.c
	baseq2/g_func.c
	baseq2/g_items.c
	baseq2/g_main.c
	baseq2/g_misc.c
	baseq2/g_monster.c
	baseq2/g_phys.c
	baseq2/g_ptrs.c
	baseq2/g_save.c
	baseq2/g_spawn.c
	baseq2/g_svcmds.c
	baseq2/g_target.c
	baseq2/g_trigger.c
	baseq2/g_turret.c
	baseq2/g_utils.c
	baseq2/g_weapon.c
	baseq2/m_actor.c
	baseq2/m_berserk.c
	baseq2/m_boss2.c
	baseq2/m_boss3.c
	baseq2/m_boss31.c
	baseq2/m_boss32.c
	baseq2/m_brain.c
	baseq2/m_chick.c
	baseq2/m_flipper.c
	baseq2/m_float.c
	baseq2/m_flyer.c
	baseq2/m_gladiator.c
	baseq2/m_gunner.c
	baseq2/m_hover.c
	baseq2/m_infantry.c
	baseq2/m_insane.c
	baseq2/m_medic.c
	baseq2/m_move.c
	baseq2/m_mutant.c
	baseq2/m_parasite.c
	baseq2/m_soldier.c
	baseq2/m_supertank.c
	baseq2/m_tank.c
	baseq2/p_client.c
	baseq2/p_hud.c
	baseq2/p_trail.c
	baseq2/p_view.c
	baseq2/p_weapon.c
)

SET(HEADERS_BASEQ2
	baseq2/g_local.h
	baseq2/g_ptrs.h
	baseq2/m_actor.h
	baseq2/m_berserk.h
	baseq2/m_boss2.h
	baseq2/m_boss31.h
	baseq2/m_boss32.h
	baseq2/m_brain.h
	baseq2/m_chick.h
	baseq2/m_flipper.h
	baseq2/m_float.h
	baseq2/m_flyer.h
	baseq2/m_gladiator.h
	baseq2/m_gunner.h
	baseq2/m_hover.h
	baseq2/m_infantry.h
	baseq2/m_insane.h
	baseq2/m_medic.h
	baseq2/m_mutant.h
	baseq2/m_parasite.h
	baseq2/m_player.h
	baseq2/m_rider.h
	baseq2/m_soldier.h
	baseq2/m_supertank.h
	baseq2/m_tank.h
)


SET(SRC_CLIENT
	client/ascii.c
	client/console.c
	client/crc.c
	client/demo.c
	client/download.c
	client/effects.c
	client/entities.c
#	client/gtv.c
#	client/http.c
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
	#client/sound/al.c
	client/sound/main.c
	client/sound/mem.c
	client/sound/mix.c
#	client/sound/qal/fixed.c
	client/sound/qal/dynamic.c
)

SET(HEADERS_CLIENT
	client/client.h
	client/ui/ui.h
	client/sound/sound.h
	client/sound/qal/dynamic.h
	client/sound/qal/fixed.h
)

SET(SRC_SERVER
#	server/ac.c
	server/commands.c
	server/entities.c
	server/game.c
	server/init.c
	server/main.c
	server/mvd.c
	server/save.c
	server/send.c
	server/user.c
	server/world.c
	server/mvd/client.c
	server/mvd/parse.c
	server/mvd/game.c
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
	common/x86/fpu.c
)

SET(HEADERS_COMMON
	common/net/inet_ntop.h
	common/net/inet_pton.h
	common/net/win.h
)

SET(SRC_REFRESH
	refresh/images.c
	refresh/models.c
)

SET(SRC_GL
	refresh/gl/draw.c
	refresh/gl/hq2x.c
	refresh/gl/images.c
	refresh/gl/main.c
	refresh/gl/mesh.c
	refresh/gl/models.c
	refresh/gl/sky.c
	refresh/gl/state.c
	refresh/gl/surf.c
	refresh/gl/tess.c
	refresh/gl/world.c
	refresh/gl/qgl/dynamic.c
#	refresh/gl/qgl/fixed.c
)

SET(HEADERS_GL
	refresh/gl/arbfp.h
	refresh/gl/gl.h
)

SET(SRC_SHARED
	shared/m_flash.c
	shared/shared.c
)

SET(SRC_WINDOWS
	windows/ac.c
#	windows/ascii.c
#windows/client.c
	windows/debug.c
	#windows/dinput.c
	#windows/dsound.c
	#windows/glimp.c
	windows/hunk.c
	#windows/swimp.c
	windows/system.c
	windows/wave.c
	#windows/wgl.c
	#unix/sdl2/sound.c
	unix/sdl2/video.c
)

SET(HEADERS_WINDOWS
	windows/wgl.h
	windows/glimp.h
	windows/client.h
	windows/threads/threads.h
)

SET(SRC_VKPT
	refresh/vkpt/asvgf.c
	refresh/vkpt/bsp_mesh.c
	refresh/vkpt/draw.c
	refresh/vkpt/light_hierarchy.c
	refresh/vkpt/main.c
	refresh/vkpt/matrix.c
	refresh/vkpt/models.c
	refresh/vkpt/path_tracer.c
	refresh/vkpt/profiler.c
	refresh/vkpt/stb.c
	refresh/vkpt/textures.c
	refresh/vkpt/uniform_buffer.c
	refresh/vkpt/vertex_buffer.c
	refresh/vkpt/vk_util.c
)

SET(HEADERS_VKPT
	refresh/vkpt/vkpt.h
	refresh/vkpt/vk_util.h
)

SET(SRC_QBVH
    common/qbvhmp.c
    refresh/glpt/bvh.c
    windows/threads/threads.c
)

SET(SRC_OPTIX
	refresh/glpt/optix.c
)

FILE(GLOB SRC_SHADERS 
	refresh/vkpt/shader/*.vert
	refresh/vkpt/shader/*.geom
	refresh/vkpt/shader/*.frag 
	refresh/vkpt/shader/*.comp
	refresh/vkpt/shader/*.glsl
	refresh/vkpt/shader/*.h
	refresh/vkpt/shader/*.rgen
	refresh/vkpt/shader/*.rchit
	refresh/vkpt/shader/*.rmiss
)

ADD_DEFINITIONS(-DHAVE_CONFIG_H=1 -DUSE_SERVER=1 -DUSE_CLIENT=1 -DCURL_STATICLIB)

ADD_LIBRARY(gamex86 SHARED ${SRC_BASEQ2} ${HEADERS_BASEQ2} ${SRC_SHARED})
ADD_EXECUTABLE(client WIN32 
	${SRC_CLIENT} ${HEADERS_CLIENT} 
	${SRC_COMMON} ${HEADERS_COMMON} 
	${SRC_REFRESH} ${SRC_SHADERS} 
	${SRC_SHARED} 
	${SRC_WINDOWS} ${HEADERS_WINDOWS}
	${SRC_SERVER} ${HEADERS_SERVER}
)

IF (CONFIG_GL_RENDERER)
	TARGET_SOURCES(client PRIVATE ${SRC_GL} ${HEADERS_GL})
	TARGET_COMPILE_DEFINITIONS(client PRIVATE REF_GL=1 USE_REF=1 VID_REF="gl")
ENDIF()

IF (CONFIG_VKPT_RENDERER)
	TARGET_SOURCES(client PRIVATE ${SRC_VKPT} ${HEADERS_VKPT})
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE refresh/vkpt/include)
	LINK_DIRECTORIES(client PRIVATE refresh/vkpt/include/vulkan)
	TARGET_COMPILE_DEFINITIONS(client PRIVATE REF_VKPT=1 USE_REF=1 VID_REF="vkpt")
	TARGET_LINK_LIBRARIES(client vulkan-1)
ENDIF()

#IF (CONFIG_USE_OPTIX)
#	TARGET_COMPILE_DEFINITIONS(client PRIVATE USE_OPTIX=1)
#	FIND_PACKAGE(CUDA REQUIRED)
#	GET_FILENAME_COMPONENT(CUDA_LIB_DIR ${CUDA_LIBRARIES} DIRECTORY)
#	TARGET_LINK_LIBRARIES(client ${CUDA_LIB_DIR}/cuda.lib ${CONFIG_OPTIX_DIR}/lib64/optix_prime.1.lib)
#	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ${CUDA_INCLUDE_DIRS} ${CONFIG_OPTIX_DIR}/include)
#	TARGET_SOURCES(client PRIVATE ${SRC_OPTIX})
#ELSE()
#	TARGET_COMPILE_DEFINITIONS(client PRIVATE ACCEL_NO_VLA=1)
#	TARGET_SOURCES(client PRIVATE ${SRC_QBVH})
#	IF (WIN32)
#		TARGET_INCLUDE_DIRECTORIES(client PRIVATE windows/threads)
#	ELSE()
#		TARGET_INCLUDE_DIRECTORIES(client PRIVATE unix/threads)
#	ENDIF()
#ENDIF()

SOURCE_GROUP("baseq2\\sources" FILES ${SRC_BASEQ2})
SOURCE_GROUP("baseq2\\headers" FILES ${HEADERS_BASEQ2})
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
	TARGET_INCLUDE_DIRECTORIES(client PRIVATE ../VC/inc)
	TARGET_INCLUDE_DIRECTORIES(gamex86 PRIVATE ../VC/inc)

	TARGET_LINK_LIBRARIES(client winmm ws2_32)

	set_target_properties(client PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
ENDIF()

TARGET_INCLUDE_DIRECTORIES(gamex86 PRIVATE ../inc)

TARGET_INCLUDE_DIRECTORIES(client PRIVATE ../inc)
TARGET_INCLUDE_DIRECTORIES(client PRIVATE "${ZLIB_INCLUDE_DIRS}")

TARGET_LINK_LIBRARIES(client PNG::png)
TARGET_LINK_LIBRARIES(client JPEG::jpeg)
TARGET_LINK_LIBRARIES(client SDL2::SDL2main SDL2::SDL2)

SET_TARGET_PROPERTIES(client
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}"
    DEBUG_POSTFIX ""
)

SET_TARGET_PROPERTIES(gamex86
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/baseq2"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/baseq2"
    DEBUG_POSTFIX ""
)

IF(IS_64_BIT)
	SET_TARGET_PROPERTIES(gamex86
		PROPERTIES
		RUNTIME_OUTPUT_NAME "gamex86_64"
	)
ENDIF()
