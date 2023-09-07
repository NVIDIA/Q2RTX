cmake_minimum_required (VERSION 3.9)
cmake_policy(SET CMP0069 NEW)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

SET(SRC_3ZB2
    g_chase.c
    g_cmds.c
    g_combat.c
    g_ctf.c
    g_func.c
    g_items.c
    g_main.c
    g_misc.c
    g_monster.c
    g_phys.c
    g_save.c
    g_spawn.c
    g_svcmds.c
    g_target.c
    g_trigger.c
    g_utils.c
    g_weapon.c
    bot/bot.c
    bot/fire.c
    bot/func.c
    bot/za.c
    monster/move.c
    player/client.c
    player/hud.c
    player/menu.c
    player/trail.c
    player/view.c
    player/weapon.c
    shared/shared.c
)

SET(HEADERS_3ZB2
    header/bot.h
    header/botstr.h
    header/ctf.h
    header/game.h
    header/local.h
    header/menu.h
    header/player.h
    header/shared.h
)

ADD_LIBRARY(3zb2 SHARED ${SRC_3ZB2} ${HEADERS_3ZB2})

SOURCE_GROUP("sources" FILES ${SRC_3ZB2})
SOURCE_GROUP("headers" FILES ${HEADERS_3ZB2})

IF (WIN32)
    TARGET_INCLUDE_DIRECTORIES(3zb2 PRIVATE ../VC/inc)
    TARGET_COMPILE_OPTIONS(3zb2 PRIVATE /wd4005 /wd4996)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(3zb2 PRIVATE ../inc)

SET_TARGET_PROPERTIES(3zb2
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/3zb2"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/3zb2"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/3zb2"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/3zb2"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/3zb2"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/3zb2"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/3zb2"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/3zb2"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/3zb2"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/3zb2"
    PREFIX ""
    DEBUG_POSTFIX ""
)

IF(IS_64_BIT)
    IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        SET_TARGET_PROPERTIES(3zb2
            PROPERTIES
            LIBRARY_OUTPUT_NAME "gameaarch64"
            RUNTIME_OUTPUT_NAME "gameaarch64"
        )
    ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
        SET_TARGET_PROPERTIES(3zb2
            PROPERTIES
            LIBRARY_OUTPUT_NAME "gameppc64le"
            RUNTIME_OUTPUT_NAME "gameppc64le"
        )
    ELSE()
        SET_TARGET_PROPERTIES(3zb2
            PROPERTIES
            LIBRARY_OUTPUT_NAME "gamex86_64"
            RUNTIME_OUTPUT_NAME "gamex86_64"
        )
    ENDIF()
ELSE()
    SET_TARGET_PROPERTIES(3zb2
        PROPERTIES
        LIBRARY_OUTPUT_NAME "gamex86"
        RUNTIME_OUTPUT_NAME "gamex86"
    )
ENDIF()
