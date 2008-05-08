# -----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

include ../config.mk

TARGET=../q2pro$(EXESUFFIX)

LDFLAGS+=-lm

SRCFILES=cmd.c cmodel.c common.c prompt.c crc.c cvar.c \
	files.c mdfour.c net_common.c net_chan.c pmove.c sv_ccmds.c 	\
	sv_ents.c sv_game.c sv_init.c sv_main.c sv_send.c	\
	sv_user.c sv_world.c sv_mvd.c sv_http.c \
	mvd_client.c mvd_parse.c mvd_game.c \
	q_msg.c q_shared.c q_uis.c q_field.c	\
	m_flash.c cl_demo.c cl_draw.c cl_ents.c cl_fx.c cl_input.c	\
	cl_locs.c cl_main.c cl_newfx.c cl_parse.c cl_pred.c cl_ref.c	\
	cl_scrn.c cl_tent.c cl_view.c cl_console.c cl_keys.c cl_aastat.c		\
	snd_main.c snd_mem.c snd_mix.c \
	ui_atoms.c ui_confirm.c ui_demos.c ui_loading.c \
	ui_menu.c ui_multiplayer.c ui_playerconfig.c ui_playermodels.c \
	ui_script.c

ifdef USE_ANTICHEAT
SRCFILES+=sv_ac.c
endif

ifdef REF_HARD_LINKED

SRCFILES+=r_images.c  \
	   r_bsp.c \
	   gl_draw.c   \
	   gl_images.c  \
	   gl_models.c \
	   gl_world.c \
	   gl_mesh.c \
	   gl_main.c  \
	   gl_state.c  \
	   gl_surf.c  \
	   gl_tess.c   \
	   gl_sky.c   \
	   qgl_api.c

ifdef USE_JPEG
LDFLAGS+=$(JPEG_LDFLAGS)
CFLAGS+=$(JPEG_CFLAGS)
endif

ifdef USE_PNG
LDFLAGS+=$(PNG_LDFLAGS)
CFLAGS+=$(PNG_CFLAGS)
endif

endif #REF_HARD_LINKED

ifdef USE_ZLIB
SRCFILES+=ioapi.c unzip.c 
LDFLAGS+=$(ZLIB_LDFLAGS)
CFLAGS+=$(ZLIB_CFLAGS)
endif

ifdef USE_ASM
ASMFILES+=snd_mixa.s math.s
endif

ifdef MINGW

SRCFILES+=sys_win.c snd_wave.c vid_win.c win_glimp.c win_wgl.c

ifndef REF_HARD_LINKED
SRCFILES+=win_swimp.c
endif

ifdef USE_DSOUND
SRCFILES+=snd_dx.c
endif

ifdef USE_DINPUT
SRCFILES+=in_dx.c
endif

LDFLAGS+=-mwindows -lws2_32 -lwinmm

RESFILES=q2pro.rc

else # MINGW

SRCFILES+=sys_unix.c

ifdef USE_DSOUND
SRCFILES+=snd_oss.c
endif

ifdef USE_DINPUT
SRCFILES+=in_evdev.c
endif

ifdef USE_DL
LDFLAGS+=-ldl
endif

ifdef USE_SDL
SRCFILES+=vid_sdl.c snd_sdl.c
CFLAGS+=$(SDL_CFLAGS)
LDFLAGS+=$(SDL_LDFLAGS)
ifdef USE_X11
CFLAGS+=$(X11_CFLAGS)
LDFLAGS+=$(X11_LDFLAGS)
endif
endif

endif # !MINGW

include $(SRCDIR)/build/target.mk

