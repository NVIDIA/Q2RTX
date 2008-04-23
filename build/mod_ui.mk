# ----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

include ../config.mk

TARGET=../mod_ui$(LIBSUFFIX)

LDFLAGS+=-lm -shared

ifdef MINGW
RESFILES+=mod_ui.rc
else
LDFLAGS+=-fPIC
CFLAGS+=-fPIC
endif

SRCFILES = q_shared.c \
	q_field.c \
	q_uis.c \
	ui_addressbook.c \
	ui_atoms.c \
	ui_confirm.c \
	ui_controls.c \
	ui_credits.c \
	ui_demos.c \
	ui_dmoptions.c \
	ui_download.c \
	ui_game.c \
	ui_ingame.c \
	ui_interface.c \
	ui_keys.c \
	ui_loadgame.c \
	ui_loading.c \
	ui_main.c \
	ui_menu.c \
	ui_mods.c \
	ui_multiplayer.c \
	ui_network.c \
	ui_options.c \
	ui_playerconfig.c \
	ui_playermodels.c \
	ui_savegame.c \
	ui_startserver.c \
	ui_video.c \
	mdfour.c

include $(SRCDIR)/build/target.mk

