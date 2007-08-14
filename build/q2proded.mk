# -----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

include ../config.mk

TARGET=$(OUTDIR)/q2proded$(EXESUFFIX)

CFLAGS+=-DDEDICATED_ONLY
LDFLAGS+=-lm

SRCFILES=cmd.c cmodel.c common.c crc.c cvar.c \
	files.c mdfour.c net_common.c net_chan.c pmove.c sv_ccmds.c \
	sv_ents.c sv_game.c sv_init.c sv_main.c sv_send.c \
	sv_user.c sv_world.c sv_mvd.c sv_http.c \
	mvd_client.c mvd_parse.c mvd_game.c \
	q_shared.c q_msg.c q_field.c prompt.c cl_null.c

ifdef USE_ZLIB
SRCFILES+=ioapi.c unzip.c 
CFLAGS+=$(ZLIB_CFLAGS)
LDFLAGS+=$(ZLIB_LDFLAGS)
endif

ifdef MINGW
SRCFILES+=sys_win.c
LDFLAGS+=-mconsole -lws2_32 -lwinmm -ladvapi32
RESFILES+=q2proded.rc
RESFLAGS+=-DDEDICATED_ONLY
else
SRCFILES+=sys_unix.c
LDFLAGS+=-ldl
endif

include $(SRCDIR)/build/target.mk

