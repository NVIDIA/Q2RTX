### Q2PRO Makefile ###

-include .config


ifdef CONFIG_WINDOWS
    CPU ?= x86
    SYS ?= Win32
else
    ifndef CPU
        CPU := $(shell uname -m | sed -e s/i.86/i386/ -e s/amd64/x86_64/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/ -e s/alpha/axp/)
    endif
    ifndef SYS
        SYS := $(shell uname -s)
    endif
endif

ifndef REV
    REV := $(shell ./version.sh --revision)
endif
ifndef VER
    VER := $(shell ./version.sh --version)
endif

CC ?= gcc
WINDRES ?= windres
STRIP ?= strip
RM ?= rm -f
RMDIR ?= rm -rf
MKDIR ?= mkdir -p

CFLAGS ?= -O2 -Wall -g -MMD $(INCLUDES)
RCFLAGS ?=
LDFLAGS ?=
LIBS ?=

CFLAGS_s := -iquote./inc
CFLAGS_c := -iquote./inc
CFLAGS_g := -iquote./inc -fno-strict-aliasing

RCFLAGS_s :=
RCFLAGS_c :=
RCFLAGS_g :=

LDFLAGS_s :=
LDFLAGS_c :=
LDFLAGS_g := -shared

ifdef CONFIG_SANITIZE
CFLAGS_c += -O0 -fsanitize=address -fno-omit-frame-pointer
LDFLAGS_c += -fsanitize=address
endif

ifdef CONFIG_VKPT_ENABLE_VALIDATION
ifneq ($(CONFIG_VKPT_ENABLE_VALIDATION),0)
CFLAGS_c += -DVKPT_ENABLE_VALIDATION
endif
endif

ifdef CONFIG_WINDOWS
    # Force i?86-netware calling convention on x86 Windows
    ifeq ($(CPU),x86)
        CONFIG_X86_GAME_ABI_HACK := y
    else
        CONFIG_X86_GAME_ABI_HACK :=
    endif

    LDFLAGS_s += -mconsole
    LDFLAGS_c += -mwindows
    LDFLAGS_g += -mconsole

    # Mark images as DEP and ASLR compatible
    LDFLAGS_s += -Wl,--nxcompat,--dynamicbase
    LDFLAGS_c += -Wl,--nxcompat,--dynamicbase
    LDFLAGS_g += -Wl,--nxcompat,--dynamicbase

    # Force relocations to be generated for 32-bit .exe files and work around
    # binutils bug that causes invalid image entry point to be set when
    # relocations are enabled.
    ifeq ($(CPU),x86)
        LDFLAGS_s += -Wl,--pic-executable,--entry,_mainCRTStartup
        LDFLAGS_c += -Wl,--pic-executable,--entry,_WinMainCRTStartup
    endif
else
    # Disable x86 features on other arches
    ifneq ($(CPU),i386)
        CONFIG_X86_GAME_ABI_HACK :=
    endif

    # Disable Linux features on other systems
    ifneq ($(SYS),Linux)
        CONFIG_DIRECT_INPUT :=
        CONFIG_NO_ICMP := y
    endif

    # Hide ELF symbols by default
    CFLAGS_s += -fvisibility=hidden
    CFLAGS_c += -fvisibility=hidden
    CFLAGS_g += -fvisibility=hidden

    # Resolve all symbols at link time
    ifeq ($(SYS),Linux)
        LDFLAGS_s += -Wl,--no-undefined
        LDFLAGS_c += -Wl,--no-undefined
        LDFLAGS_g += -Wl,--no-undefined
    endif

    CFLAGS_g += -fPIC
endif

BUILD_DEFS := -DCPUSTRING='"$(CPU)"'
BUILD_DEFS += -DBUILDSTRING='"$(SYS)"'

VER_DEFS := -DREVISION=$(REV)
VER_DEFS += -DVERSION='"$(VER)"'

CONFIG_GAME_BASE ?= baseq2
CONFIG_GAME_DEFAULT ?=
PATH_DEFS := -DBASEGAME='"$(CONFIG_GAME_BASE)"'
PATH_DEFS += -DDEFGAME='"$(CONFIG_GAME_DEFAULT)"'

# System paths
ifndef CONFIG_WINDOWS
    CONFIG_PATH_DATA ?= .
    CONFIG_PATH_LIB ?= .
    CONFIG_PATH_HOME ?=
    PATH_DEFS += -DDATADIR='"$(CONFIG_PATH_DATA)"'
    PATH_DEFS += -DLIBDIR='"$(CONFIG_PATH_LIB)"'
    PATH_DEFS += -DHOMEDIR='"$(CONFIG_PATH_HOME)"'
endif

CFLAGS_s += $(BUILD_DEFS) $(VER_DEFS) $(PATH_DEFS) -DUSE_SERVER=1 -DUSE_CLIENT=0
CFLAGS_c += $(BUILD_DEFS) $(VER_DEFS) $(PATH_DEFS) -DUSE_SERVER=1 -DUSE_CLIENT=1

# windres needs special quoting...
RCFLAGS_s += -DREVISION=$(REV) -DVERSION='\"$(VER)\"'
RCFLAGS_c += -DREVISION=$(REV) -DVERSION='\"$(VER)\"'
RCFLAGS_g += -DREVISION=$(REV) -DVERSION='\"$(VER)\"'


### Object Files ###

COMMON_OBJS := \
    src/common/bsp.o        \
    src/common/cmd.o        \
    src/common/cmodel.o     \
    src/common/common.o     \
    src/common/cvar.o       \
    src/common/error.o      \
    src/common/field.o      \
    src/common/fifo.o       \
    src/common/files.o      \
    src/common/math.o       \
    src/common/mdfour.o     \
    src/common/msg.o        \
    src/common/net/chan.o   \
    src/common/net/net.o    \
    src/common/pmove.o      \
    src/common/prompt.o     \
    src/common/sizebuf.o    \
    src/common/utils.o      \
    src/common/zone.o       \
    src/shared/shared.o

OBJS_c := \
    $(COMMON_OBJS)          \
    src/shared/m_flash.o    \
    src/client/ascii.o      \
    src/client/console.o    \
    src/client/crc.o        \
    src/client/demo.o       \
    src/client/download.o   \
    src/client/effects.o    \
    src/client/entities.o   \
    src/client/input.o      \
    src/client/keys.o       \
    src/client/locs.o       \
    src/client/main.o       \
    src/client/newfx.o      \
    src/client/parse.o      \
    src/client/precache.o   \
    src/client/predict.o    \
    src/client/refresh.o    \
    src/client/screen.o     \
    src/client/tent.o       \
    src/client/view.o       \
    src/client/sound/main.o \
    src/client/sound/mem.o  \
    src/refresh/images.o    \
    src/refresh/models.o    \
    src/server/commands.o   \
    src/server/entities.o   \
    src/server/game.o       \
    src/server/init.o       \
    src/server/save.o       \
    src/server/send.o       \
    src/server/main.o       \
    src/server/user.o       \
    src/server/world.o      \

OBJS_s := \
    $(COMMON_OBJS)  \
    src/client/null.o       \
    src/server/commands.o   \
    src/server/entities.o   \
    src/server/game.o       \
    src/server/init.o       \
    src/server/send.o       \
    src/server/main.o       \
    src/server/user.o       \
    src/server/world.o

OBJS_g := \
    src/shared/shared.o         \
    src/shared/m_flash.o        \
    src/baseq2/g_ai.o           \
    src/baseq2/g_chase.o        \
    src/baseq2/g_cmds.o         \
    src/baseq2/g_combat.o       \
    src/baseq2/g_func.o         \
    src/baseq2/g_items.o        \
    src/baseq2/g_main.o         \
    src/baseq2/g_misc.o         \
    src/baseq2/g_monster.o      \
    src/baseq2/g_phys.o         \
    src/baseq2/g_ptrs.o         \
    src/baseq2/g_save.o         \
    src/baseq2/g_spawn.o        \
    src/baseq2/g_svcmds.o       \
    src/baseq2/g_target.o       \
    src/baseq2/g_trigger.o      \
    src/baseq2/g_turret.o       \
    src/baseq2/g_utils.o        \
    src/baseq2/g_weapon.o       \
    src/baseq2/m_actor.o        \
    src/baseq2/m_berserk.o      \
    src/baseq2/m_boss2.o        \
    src/baseq2/m_boss31.o       \
    src/baseq2/m_boss32.o       \
    src/baseq2/m_boss3.o        \
    src/baseq2/m_brain.o        \
    src/baseq2/m_chick.o        \
    src/baseq2/m_flipper.o      \
    src/baseq2/m_float.o        \
    src/baseq2/m_flyer.o        \
    src/baseq2/m_gladiator.o    \
    src/baseq2/m_gunner.o       \
    src/baseq2/m_hover.o        \
    src/baseq2/m_infantry.o     \
    src/baseq2/m_insane.o       \
    src/baseq2/m_medic.o        \
    src/baseq2/m_move.o         \
    src/baseq2/m_mutant.o       \
    src/baseq2/m_parasite.o     \
    src/baseq2/m_soldier.o      \
    src/baseq2/m_supertank.o    \
    src/baseq2/m_tank.o         \
    src/baseq2/p_client.o       \
    src/baseq2/p_hud.o          \
    src/baseq2/p_trail.o        \
    src/baseq2/p_view.o         \
    src/baseq2/p_weapon.o


### Configuration Options ###

# cd tracks via ogg
ifdef CONFIG_OGG
    OBJS_c += src/client/sound/ogg.o
    CFLAGS_c += -DOGG
    LIBS_c += -lvorbisfile -lvorbis -logg
endif

ifdef CONFIG_HTTP
    CURL_CFLAGS ?= $(shell pkg-config libcurl --cflags)
    CURL_LIBS ?= $(shell pkg-config libcurl --libs)
    CFLAGS_c += -DUSE_CURL=1 $(CURL_CFLAGS)
    LIBS_c += $(CURL_LIBS)
    OBJS_c += src/client/http.o
endif

ifdef CONFIG_CLIENT_GTV
    CFLAGS_c += -DUSE_CLIENT_GTV=1
    OBJS_c += src/client/gtv.o
endif

ifndef CONFIG_NO_SOFTWARE_SOUND
    CFLAGS_c += -DUSE_SNDDMA=1
    OBJS_c += src/client/sound/mix.o
    OBJS_c += src/client/sound/dma.o
endif

ifdef CONFIG_OPENAL
    CFLAGS_c += -DUSE_OPENAL=1
    OBJS_c += src/client/sound/al.o
    ifdef CONFIG_FIXED_LIBAL
        AL_CFLAGS ?= $(shell pkg-config openal --cflags)
        AL_LIBS ?= $(shell pkg-config openal --libs)
        CFLAGS_c += -DUSE_FIXED_LIBAL=1 $(AL_CFLAGS)
        LIBS_c += $(AL_LIBS)
        OBJS_c += src/client/sound/qal/fixed.o
    else
        OBJS_c += src/client/sound/qal/dynamic.o
    endif
endif

ifndef CONFIG_NO_MENUS
    CFLAGS_c += -DUSE_UI=1
    OBJS_c += src/client/ui/demos.o
    OBJS_c += src/client/ui/menu.o
    OBJS_c += src/client/ui/playerconfig.o
    OBJS_c += src/client/ui/playermodels.o
    OBJS_c += src/client/ui/script.o
    OBJS_c += src/client/ui/servers.o
    OBJS_c += src/client/ui/ui.o
endif

# Light styles are always enabled
CFLAGS_c += -DUSE_LIGHTSTYLES=1

ifndef CONFIG_NO_DYNAMIC_LIGHTS
    CFLAGS_c += -DUSE_DLIGHTS=1
endif

ifndef CONFIG_NO_AUTOREPLY
    CFLAGS_c += -DUSE_AUTOREPLY=1
endif

ifndef CONFIG_NO_MAPCHECKSUM
    CFLAGS_c += -DUSE_MAPCHECKSUM=1
endif

ifdef CONFIG_SOFTWARE_RENDERER
    CFLAGS_c += -DREF_SOFT=1 -DUSE_REF=1 -DVID_REF='"soft"'
    OBJS_c += src/refresh/sw/aclip.o
    OBJS_c += src/refresh/sw/alias.o
    OBJS_c += src/refresh/sw/bsp.o
    OBJS_c += src/refresh/sw/draw.o
    OBJS_c += src/refresh/sw/edge.o
    OBJS_c += src/refresh/sw/image.o
    OBJS_c += src/refresh/sw/light.o
    OBJS_c += src/refresh/sw/main.o
    OBJS_c += src/refresh/sw/misc.o
    OBJS_c += src/refresh/sw/model.o
    OBJS_c += src/refresh/sw/part.o
    OBJS_c += src/refresh/sw/poly.o
    OBJS_c += src/refresh/sw/polyset.o
    OBJS_c += src/refresh/sw/raster.o
    OBJS_c += src/refresh/sw/scan.o
    OBJS_c += src/refresh/sw/surf.o
    OBJS_c += src/refresh/sw/sird.o
    OBJS_c += src/refresh/sw/sky.o
endif
ifdef CONFIG_GL_RENDERER
    CFLAGS_c += -DREF_GL=1 -DUSE_REF=1 -DVID_REF='"gl"'
    OBJS_c += src/refresh/gl/draw.o
    OBJS_c += src/refresh/gl/hq2x.o
    OBJS_c += src/refresh/gl/images.o
    OBJS_c += src/refresh/gl/main.o
    OBJS_c += src/refresh/gl/mesh.o
    OBJS_c += src/refresh/gl/models.o
    OBJS_c += src/refresh/gl/sky.o
    OBJS_c += src/refresh/gl/state.o
    OBJS_c += src/refresh/gl/surf.o
    OBJS_c += src/refresh/gl/tess.o
    OBJS_c += src/refresh/gl/world.o
    ifdef CONFIG_FIXED_LIBGL
        GL_CFLAGS ?=
        GL_LIBS ?= -lGL
        CFLAGS_c += -DUSE_FIXED_LIBGL=1 $(GL_CFLAGS)
        LIBS_c += $(GL_LIBS)
        OBJS_c += src/refresh/gl/qgl/fixed.o
    else
        OBJS_c += src/refresh/gl/qgl/dynamic.o
    endif
endif
ifdef CONFIG_VKPT_RENDERER
    VKPT_SHADER_DIR=shader_vkpt
    CFLAGS_c += -DREF_VKPT=1 -DUSE_REF=1 -DVID_REF='"vkpt"' -Isrc/refresh/vkpt/ -DVKPT_SHADER_DIR='"$(VKPT_SHADER_DIR)"'
    LIBS_c +=-lvulkan
    OBJS_c += src/refresh/vkpt/main.o
    OBJS_c += src/refresh/vkpt/textures.o
    OBJS_c += src/refresh/vkpt/draw.o
    OBJS_c += src/refresh/vkpt/matrix.o
    OBJS_c += src/refresh/vkpt/models.o
    OBJS_c += src/refresh/vkpt/path_tracer.o
    OBJS_c += src/refresh/vkpt/vk_util.o
    OBJS_c += src/refresh/vkpt/bsp_mesh.o
    OBJS_c += src/refresh/vkpt/uniform_buffer.o
    OBJS_c += src/refresh/vkpt/vertex_buffer.o
    OBJS_c += src/refresh/vkpt/light_hierarchy.o
    OBJS_c += src/refresh/vkpt/asvgf.o
    OBJS_c += src/refresh/vkpt/stb.o
    OBJS_c += src/refresh/vkpt/profiler.o

    VKPT_SHADER_SRC = $(shell find src/refresh/vkpt/shader -type f | egrep '\.(vert|frag|geom|rchit|rgen|rmiss|rcall|comp)$$' | sed s!.*/!!)
    VKPT_SHADER_HDR = $(shell find src/refresh/vkpt/shader -type f | egrep '\.(h|glsl)$$')
    VKPT_SHADER_SPV = $(VKPT_SHADER_SRC:%=$(VKPT_SHADER_DIR)/%.spv)

endif

CONFIG_DEFAULT_MODELIST ?= 640x480 800x600 1024x768
CONFIG_DEFAULT_GEOMETRY ?= 640x480
CFLAGS_c += -DVID_MODELIST='"$(CONFIG_DEFAULT_MODELIST)"'
CFLAGS_c += -DVID_GEOMETRY='"$(CONFIG_DEFAULT_GEOMETRY)"'

ifndef CONFIG_SOFTWARE_RENDERER
    ifndef CONFIG_NO_MD3
        CFLAGS_c += -DUSE_MD3=1
    endif
endif

ifdef CONFIG_NO_BINDLESS_TEXTURES
    CFLAGS_c += -DNO_BINDLESS_TEXTURES=1
endif

ifdef CONFIG_SMALL_GPU
    CFLAGS_c += -DUSE_SMALL_GPU=1
endif

ifndef CONFIG_NO_TGA
    CFLAGS_c += -DUSE_TGA=1
endif

ifdef CONFIG_PNG
    PNG_CFLAGS ?= $(shell libpng-config --cflags)
    PNG_LIBS ?= $(shell libpng-config --libs)
    CFLAGS_c += -DUSE_PNG=1 $(PNG_CFLAGS)
    LIBS_c += $(PNG_LIBS)
endif

ifdef CONFIG_JPEG
    JPG_CFLAGS ?=
    JPG_LIBS ?= -ljpeg
    CFLAGS_c += -DUSE_JPG=1 $(JPG_CFLAGS)
    LIBS_c += $(JPG_LIBS)
endif

ifdef CONFIG_ANTICHEAT_SERVER
    CFLAGS_s += -DUSE_AC_SERVER=1
    OBJS_s += src/server/ac.o
endif

ifdef CONFIG_MVD_SERVER
    CFLAGS_s += -DUSE_MVD_SERVER=1
    CFLAGS_c += -DUSE_MVD_SERVER=1
    OBJS_s += src/server/mvd.o
    OBJS_c += src/server/mvd.o
endif

ifdef CONFIG_MVD_CLIENT
    CFLAGS_s += -DUSE_MVD_CLIENT=1
    CFLAGS_c += -DUSE_MVD_CLIENT=1
    OBJS_s += src/server/mvd/client.o src/server/mvd/game.o src/server/mvd/parse.o
    OBJS_c += src/server/mvd/client.o src/server/mvd/game.o src/server/mvd/parse.o
endif

ifdef CONFIG_NO_ZLIB
    CFLAGS_c += -DUSE_ZLIB=0
    CFLAGS_s += -DUSE_ZLIB=0
else
    ZLIB_CFLAGS ?=
    ZLIB_LIBS ?= -lz
    CFLAGS_c += -DUSE_ZLIB=1 $(ZLIB_CFLAGS)
    CFLAGS_s += -DUSE_ZLIB=1 $(ZLIB_CFLAGS)
    LIBS_c += $(ZLIB_LIBS)
    LIBS_s += $(ZLIB_LIBS)
endif

ifndef CONFIG_NO_ICMP
    CFLAGS_c += -DUSE_ICMP=1
    CFLAGS_s += -DUSE_ICMP=1
endif

ifndef CONFIG_NO_SYSTEM_CONSOLE
    CFLAGS_c += -DUSE_SYSCON=1
    CFLAGS_s += -DUSE_SYSCON=1
endif

ifdef CONFIG_X86_GAME_ABI_HACK
    CFLAGS_c += -DUSE_GAME_ABI_HACK=1
    CFLAGS_s += -DUSE_GAME_ABI_HACK=1
    CFLAGS_g += -DUSE_GAME_ABI_HACK=1
endif

ifdef CONFIG_VARIABLE_SERVER_FPS
    CFLAGS_c += -DUSE_FPS=1
    CFLAGS_s += -DUSE_FPS=1
endif

ifdef CONFIG_WINDOWS
    OBJS_c += src/windows/client.o

    ifdef CONFIG_DIRECT_INPUT
        CFLAGS_c += -DUSE_DINPUT=1
        OBJS_c += src/windows/dinput.o
    endif

    ifndef CONFIG_NO_SOFTWARE_SOUND
        OBJS_c += src/windows/wave.o
        ifdef CONFIG_DIRECT_SOUND
            CFLAGS_c += -DUSE_DSOUND=1
            OBJS_c += src/windows/dsound.o
        endif
    endif

    ifdef CONFIG_SOFTWARE_RENDERER
        OBJS_c += src/windows/swimp.o
    endif
    ifdef CONFIG_GL_RENDERER
        OBJS_c += src/windows/glimp.o
        OBJS_c += src/windows/wgl.o
    endif

    ifdef CONFIG_WINDOWS_CRASH_DUMPS
        CFLAGS_c += -DUSE_DBGHELP=1
        CFLAGS_s += -DUSE_DBGHELP=1
        OBJS_c += src/windows/debug.o
        OBJS_s += src/windows/debug.o
    endif

    ifdef CONFIG_WINDOWS_SERVICE
        CFLAGS_s += -DUSE_WINSVC=1
    endif

    OBJS_c += src/windows/hunk.o src/windows/system.o
    OBJS_s += src/windows/hunk.o src/windows/system.o

    # Resources
    OBJS_c += src/windows/res/q2pro.o
    OBJS_s += src/windows/res/q2proded.o
    OBJS_g += src/windows/res/baseq2.o

    # System libs
    LIBS_s += -lws2_32 -lwinmm -ladvapi32
    LIBS_c += -lws2_32 -lwinmm
else
    ifdef CONFIG_SDL2
        SDL_CFLAGS ?= $(shell sdl2-config --cflags)
        SDL_LIBS ?= $(shell sdl2-config --libs)
        CFLAGS_c += -DUSE_SDL=2 $(SDL_CFLAGS)
        LIBS_c += $(SDL_LIBS)
        OBJS_c += src/unix/sdl2/video.o
    else
        SDL_CFLAGS ?= $(shell sdl-config --cflags)
        SDL_LIBS ?= $(shell sdl-config --libs)
        CFLAGS_c += -DUSE_SDL=1 $(SDL_CFLAGS)
        LIBS_c += $(SDL_LIBS)
        OBJS_c += src/unix/sdl/video.o
        OBJS_c += src/unix/sdl/clipboard.o

        ifdef CONFIG_SOFTWARE_RENDERER
            OBJS_c += src/unix/sdl/swimp.o
        endif
        ifdef CONFIG_GL_RENDERER
            OBJS_c += src/unix/sdl/glimp.o
        endif

        ifdef CONFIG_X11
            X11_CFLAGS ?=
            X11_LIBS ?= -lX11
            CFLAGS_c += -DUSE_X11=1 $(X11_CFLAGS)
            LIBS_c += $(X11_LIBS)
            ifdef CONFIG_GL_RENDERER
                OBJS_c += src/unix/sdl/glx.o
            endif
        endif

        ifdef CONFIG_DIRECT_INPUT
            CFLAGS_c += -DUSE_DINPUT=1
            OBJS_c += src/unix/evdev.o
            ifndef CONFIG_NO_UDEV
                UDEV_CFLAGS ?=
                UDEV_LIBS ?= -ludev
                CFLAGS_c += -DUSE_UDEV=1 $(UDEV_CFLAGS)
                LIBS_c += $(UDEV_LIBS)
            endif
        endif
    endif

    ifdef CONFIG_LIRC
        LIRC_CFLAGS ?=
        LIRC_LIBS ?= -llirc_client
        CFLAGS_c += -DUSE_LIRC=1 $(LIRC_CFLAGS)
        OBJS_c += src/unix/lirc.o
        LIBS_c += $(LIRC_LIBS)
    endif

    ifndef CONFIG_NO_SOFTWARE_SOUND
        ifdef CONFIG_SDL2
            OBJS_c += src/unix/sdl2/sound.o
        else
            OBJS_c += src/unix/sdl/sound.o
        endif
        ifdef CONFIG_DIRECT_SOUND
            CFLAGS_c += -DUSE_DSOUND=1
            OBJS_c += src/unix/oss.o
        endif
    endif

    OBJS_s += src/unix/hunk.o src/unix/system.o
    OBJS_c += src/unix/hunk.o src/unix/system.o

    ifndef CONFIG_NO_SYSTEM_CONSOLE
        OBJS_s += src/unix/tty.o
        OBJS_c += src/unix/tty.o
    endif

    # System libs
    LIBS_s += -lm
    LIBS_c += -lm
    LIBS_g += -lm

    ifeq ($(SYS),Linux)
        LIBS_s += -ldl
        LIBS_c += -ldl
    endif
endif

ifdef CONFIG_TESTS
    CFLAGS_c += -DUSE_TESTS=1
    CFLAGS_s += -DUSE_TESTS=1
    OBJS_c += src/common/tests.o
    OBJS_s += src/common/tests.o
endif

ifdef CONFIG_DEBUG
    CFLAGS_c += -D_DEBUG
    CFLAGS_s += -D_DEBUG
endif

ifdef CONFIG_GL_DEBUG
    CFLAGS_c += -D_GL_DEBUG
endif

ifeq ($(CPU),x86)
    OBJS_c += src/common/x86/fpu.o
    OBJS_s += src/common/x86/fpu.o
endif

ifeq ($(CPU),i386)
    OBJS_c += src/common/x86/fpu.o
    OBJS_s += src/common/x86/fpu.o
endif

### Targets ###

ifdef CONFIG_WINDOWS
    TARG_s := q2proded.exe
    TARG_c := q2pro.exe
    TARG_g := game$(CPU).dll
    TARG_t :=
else
    TARG_s := vkptded
    TARG_c := q2vkpt
    TARG_g := game$(CPU).so
    TARG_t := 
endif

all: $(TARG_s) $(TARG_c) $(TARG_g)

default: all

.PHONY: all default clean strip

# Define V=1 to show command line.
ifdef V
    Q :=
    E := @true
else
    Q := @
    E := @echo
endif

# Temporary build directories
BUILD_s := .q2proded
BUILD_c := .q2pro
BUILD_g := .baseq2

# Rewrite paths to build directories
OBJS_s := $(patsubst %,$(BUILD_s)/%,$(OBJS_s))
OBJS_c := $(patsubst %,$(BUILD_c)/%,$(OBJS_c))
OBJS_g := $(patsubst %,$(BUILD_g)/%,$(OBJS_g))

DEPS_s := $(OBJS_s:.o=.d)
DEPS_c := $(OBJS_c:.o=.d)
DEPS_g := $(OBJS_g:.o=.d)


ifdef CONFIG_VKPT_RENDERER
$(TARG_c): $(VKPT_SHADER_SPV)
compile_shaders: $(VKPT_SHADER_SPV)

CONFIG_GLSLANGVALIDATOR ?= glslangValidator

$(VKPT_SHADER_DIR)/%.spv: src/refresh/vkpt/shader/% $(VKPT_SHADER_HDR)
	$(E) [GLSL] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CONFIG_GLSLANGVALIDATOR) \
		--target-env vulkan1.1 \
		-DVKPT_SHADER \
		-DSHADER_STAGE_$$(echo $< | sed 's/.*\.//' | tr 'a-z' 'A-Z') \
		-V $< -o $@ | sed 1d

endif

-include $(DEPS_s)
-include $(DEPS_c)
-include $(DEPS_g)

clean:
	$(E) [CLEAN]
	$(Q)$(RM) $(TARG_s) $(TARG_c) $(TARG_g)
	$(Q)$(RM) $(VKPT_SHADER_SPV)
	$(Q)$(RMDIR) $(BUILD_s) $(BUILD_c) $(BUILD_g)

strip: $(TARG_s) $(TARG_c) $(TARG_g)
	$(E) [STRIP]
	$(Q)$(STRIP) $(TARG_s) $(TARG_c) $(TARG_g)

# ------

$(BUILD_s)/%.o: %.c
	$(E) [CC] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CC) -c $(CFLAGS) $(CFLAGS_s) -o $@ $<

$(BUILD_s)/%.o: %.rc
	$(E) [RC] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(WINDRES) $(RCFLAGS) $(RCFLAGS_s) -o $@ $<

$(TARG_s): $(OBJS_s)
	$(E) [LD] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CC) $(LDFLAGS) $(LDFLAGS_s) -o $@ $(OBJS_s) $(LIBS) $(LIBS_s)

# ------

$(BUILD_c)/%.o: %.c
	$(E) [CC] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CC) -c $(CFLAGS) $(CFLAGS_c) -o $@ $<

$(BUILD_c)/%.o: %.rc
	$(E) [RC] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(WINDRES) $(RCFLAGS) $(RCFLAGS_c) -o $@ $<

$(TARG_c): $(OBJS_c)
	$(E) [LD] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CC) $(LDFLAGS) $(LDFLAGS_c) -o $@ $(OBJS_c) $(LIBS) $(LIBS_c)

# ------

$(BUILD_g)/%.o: %.c
	$(E) [CC] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CC) -c $(CFLAGS) $(CFLAGS_g) -o $@ $<

$(BUILD_g)/%.o: %.rc
	$(E) [RC] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(WINDRES) $(RCFLAGS) $(RCFLAGS_g) -o $@ $<

$(TARG_g): $(OBJS_g)
	$(E) [LD] $@
	$(Q)$(MKDIR) $(@D)
	$(Q)$(CC) $(LDFLAGS) $(LDFLAGS_g) -o $@ $(OBJS_g) $(LIBS) $(LIBS_g)

