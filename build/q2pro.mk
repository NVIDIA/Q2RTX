# -----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

include ../config.mk

TARGET=$(OUTDIR)/q2pro$(EXESUFFIX)

LDFLAGS+=-lm

SRCFILES=cmd.c cmodel.c common.c prompt.c crc.c cvar.c \
	files.c mdfour.c net_common.c net_chan.c pmove.c sv_ccmds.c 	\
	sv_ents.c sv_game.c sv_init.c sv_main.c sv_send.c	\
	sv_user.c sv_world.c sv_mvd.c sv_http.c \
	mvd_client.c mvd_parse.c mvd_game.c \
	q_msg.c q_shared.c q_uis.c q_field.c q_lex.c	\
	m_flash.c cl_cin.c cl_demo.c cl_draw.c cl_ents.c cl_fx.c cl_input.c	\
	cl_locs.c cl_main.c cl_newfx.c cl_parse.c cl_pred.c cl_ref.c	\
	cl_scrn.c cl_tent.c cl_ui.c cl_view.c cl_console.c cl_keys.c		\
	snd_main.c snd_mem.c snd_mix.c

ifdef USE_ZLIB
SRCFILES+=ioapi.c unzip.c 
LDFLAGS+=$(ZLIB_LDFLAGS)
CFLAGS+=$(ZLIB_CFLAGS)
endif

ifdef USE_ASM
ASMFILES+=snd_mixa.s
endif

ifdef MINGW

SRCFILES+=sys_win.c snd_wave.c vid_win.c win_glimp.c win_wgl.c win_swimp.c

ifdef USE_DSOUND
SRCFILES+=snd_dx.c
endif

ifdef USE_DINPUT
SRCFILES+=in_dx.c
endif

LDFLAGS+=-mwindows -lws2_32 -lwinmm

RESFILES=q2pro.rc

else

SRCFILES+=sys_unix.c

ifdef USE_OSS
SRCFILES+=snd_oss.c
endif

ifdef USE_EVDEV
SRCFILES+=in_evdev.c
endif

ASMFILES+=snd_mixa.s
LDFLAGS+=-ldl

ifdef USE_SDL
SRCFILES+=vid_sdl.c
CFLAGS+=$(SDL_CFLAGS)
LDFLAGS+=-lX11 $(SDL_LDFLAGS)
endif

endif

include $(SRCDIR)/build/target.mk

