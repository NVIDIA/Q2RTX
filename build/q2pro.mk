# Copyright (C) 2008 Andrey Nazarov
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
#
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

include ../config.mk

TARGET=../q2pro$(EXESUFFIX)

LDFLAGS+=-lm
CFLAGS+=-DUSE_CLIENT=1

include $(SRCDIR)/build/common.mk

SRCFILES+=m_flash.c \
	cl_demo.c \
	cl_ents.c \
	cl_fx.c \
	cl_input.c \
	cl_locs.c \
	cl_main.c \
	cl_newfx.c \
	cl_parse.c \
	cl_pred.c \
	cl_ref.c \
	cl_scrn.c \
	cl_tent.c \
	cl_view.c \
	cl_console.c \
	cl_keys.c \
	cl_aastat.c

ifdef USE_CURL
SRCFILES+=cl_http.c
CFLAGS+=$(CURL_CFLAGS)
LDFLAGS+=$(CURL_LDFLAGS)
endif
	
SRCFILES+=snd_main.c snd_mem.c 

ifdef USE_SNDDMA
SRCFILES+=snd_mix.c snd_dma.c
endif

ifdef USE_OPENAL
SRCFILES+=snd_al.c qal_api.c
endif

ifdef USE_UI
SRCFILES+=ui_atoms.c \
	ui_confirm.c \
	ui_demos.c \
	ui_menu.c \
	ui_multiplayer.c \
	ui_playerconfig.c \
	ui_playermodels.c \
	ui_script.c
endif

ifdef USE_REF
SRCFILES+=r_images.c r_models.c
include $(SRCDIR)/build/ref_$(USE_REF).mk
endif

# ifdef USE_SERVER
include $(SRCDIR)/build/server.mk
SRCFILES+=sv_save.c
# endif

ifdef MINGW

SRCFILES+=sys_win.c

ifdef USE_REF
SRCFILES+=vid_win.c
endif

ifdef USE_SNDDMA
SRCFILES+=snd_wave.c
endif

ifdef USE_DSOUND
SRCFILES+=snd_dx.c
endif

ifdef USE_DINPUT
SRCFILES+=in_dx.c
endif

LDFLAGS+=-mwindows
ifdef WINCE
SRCFILES+=win_ascii.c
LDFLAGS+=-lwinsock -lmmtimer
else
LDFLAGS+=-lws2_32 -lwinmm
endif

RESFILES=q2pro.rc

else # MINGW

SRCFILES+=sys_unix.c

ifdef USE_DSOUND
SRCFILES+=snd_oss.c
endif

ifdef USE_DINPUT
SRCFILES+=in_evdev.c
endif

ifdef USE_LIRC
SRCFILES+=in_lirc.c
LDFLAGS+=-llirc_client
endif

ifdef USE_DL
LDFLAGS+=-ldl
endif

ifdef USE_SDL
SRCFILES+=vid_sdl.c
ifdef USE_SNDDMA
SRCFILES+=snd_sdl.c
endif
CFLAGS+=$(SDL_CFLAGS)
LDFLAGS+=$(SDL_LDFLAGS)
ifdef USE_X11
CFLAGS+=$(X11_CFLAGS)
LDFLAGS+=$(X11_LDFLAGS)
endif
endif

endif # !MINGW

include $(SRCDIR)/build/target.mk

