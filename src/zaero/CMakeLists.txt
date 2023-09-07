cmake_minimum_required (VERSION 3.9)
cmake_policy(SET CMP0069 NEW)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_definitions(-DYQ2OSTYPE="${CMAKE_SYSTEM_NAME}")

# Architecture string
string(REGEX REPLACE "amd64" "x86_64" YQ2_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
string(REGEX REPLACE "i.86" "i386" YQ2_ARCH "${YQ2_ARCH}")
string(REGEX REPLACE "^arm.*" "arm" YQ2_ARCH "${YQ2_ARCH}")
add_definitions(-DYQ2ARCH="${YQ2_ARCH}")

SET(SRC_ZAERO
    g_ai.c
    g_cmds.c
    g_combat.c
    g_func.c
    g_items.c
    g_main.c
    g_misc.c
    g_monster.c
    g_phys.c
    g_spawn.c
    g_svcmds.c
    g_target.c
    g_trigger.c
    g_turret.c
    g_utils.c
    g_weapon.c
    monster/actor/actor.c
    monster/berserker/berserker.c
    monster/boss/boss.c
    monster/boss2/boss2.c
    monster/boss3/boss3.c
    monster/boss3/boss31.c
    monster/boss3/boss32.c
    monster/brain/brain.c
    monster/chick/chick.c
    monster/flipper/flipper.c
    monster/float/float.c
    monster/flyer/flyer.c
    monster/gladiator/gladiator.c
    monster/gunner/gunner.c
    monster/handler/handler.c
    monster/hound/hound.c
    monster/hover/hover.c
    monster/infantry/infantry.c
    monster/insane/insane.c
    monster/medic/medic.c
    monster/misc/move.c
    monster/mutant/mutant.c
    monster/parasite/parasite.c
    monster/sentien/sentien.c
    monster/soldier/soldier.c
    monster/supertank/supertank.c
    monster/tank/tank.c
    player/client.c
    player/hud.c
    player/trail.c
    player/view.c
    player/weapon.c
    savegame/savegame.c
    shared/flash.c
    shared/shared.c
    zaero/acannon.c
    zaero/ai.c
    zaero/anim.c
    zaero/camera.c
    zaero/frames.c
    zaero/item.c
    zaero/mtest.c
    zaero/spawn.c
    zaero/trigger.c
    zaero/weapon.c
)

SET(HEADERS_ZAERO
    header/anim.h
    header/frames.h
    header/game.h
    header/local.h
    header/shared.h
    monster/actor/actor.h
    monster/berserker/berserker.h
    monster/boss/boss.h
    monster/boss2/boss2.h
    monster/boss3/boss31.h
    monster/boss3/boss32.h
    monster/brain/brain.h
    monster/chick/chick.h
    monster/flipper/flipper.h
    monster/float/float.h
    monster/flyer/flyer.h
    monster/gladiator/gladiator.h
    monster/gunner/gunner.h
    monster/handler/handler.h
    monster/hound/hound.h
    monster/hover/hover.h
    monster/infantry/infantry.h
    monster/insane/insane.h
    monster/medic/medic.h
    monster/misc/player.h
    monster/mutant/mutant.h
    monster/parasite/parasite.h
    monster/sentien/sentien.h
    monster/soldier/soldier.h
    monster/supertank/supertank.h
    savegame/tables/clientfields.h
    savegame/tables/fields.h
    savegame/tables/gamefunc_decs.h
    savegame/tables/gamefunc_list.h
    savegame/tables/gamemmove_decs.h
    savegame/tables/gamemmove_list.h
    savegame/tables/levelfields.h
)

ADD_LIBRARY(zaero SHARED ${SRC_ZAERO} ${HEADERS_ZAERO})

SOURCE_GROUP("sources" FILES ${SRC_ZAERO})
SOURCE_GROUP("headers" FILES ${HEADERS_ZAERO})

IF (WIN32)
    TARGET_INCLUDE_DIRECTORIES(zaero PRIVATE ../VC/inc)
    TARGET_COMPILE_OPTIONS(zaero PRIVATE /wd4005 /wd4996)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(zaero PRIVATE ../inc)

SET_TARGET_PROPERTIES(zaero
    PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/zaero"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/zaero"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/zaero"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/zaero"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/zaero"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/zaero"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/zaero"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/zaero"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/zaero"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/zaero"
    PREFIX ""
    DEBUG_POSTFIX ""
)

IF(IS_64_BIT)
    IF(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        SET_TARGET_PROPERTIES(zaero
            PROPERTIES
            LIBRARY_OUTPUT_NAME "gameaarch64"
            RUNTIME_OUTPUT_NAME "gameaarch64"
        )
    ELSEIF(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
        SET_TARGET_PROPERTIES(zaero
            PROPERTIES
            LIBRARY_OUTPUT_NAME "gameppc64le"
            RUNTIME_OUTPUT_NAME "gameppc64le"
        )
    ELSE()
        SET_TARGET_PROPERTIES(zaero
            PROPERTIES
            LIBRARY_OUTPUT_NAME "gamex86_64"
            RUNTIME_OUTPUT_NAME "gamex86_64"
        )
    ENDIF()
ELSE()
    SET_TARGET_PROPERTIES(zaero
        PROPERTIES
        LIBRARY_OUTPUT_NAME "gamex86"
        RUNTIME_OUTPUT_NAME "gamex86"
    )
ENDIF()
