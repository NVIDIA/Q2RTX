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

CFLAGS+=-DUSE_SERVER=1

SRCFILES+=sv_ccmds.c \
	sv_ents.c \
	sv_game.c \
	sv_init.c \
	sv_main.c \
	sv_send.c \
	sv_user.c \
	sv_world.c

ifdef USE_AC_SERVER
SRCFILES+=sv_ac.c
endif

ifdef USE_MVD_SERVER
SRCFILES+=sv_mvd.c
endif

ifdef USE_MVD_CLIENT
SRCFILES+=mvd_client.c \
	mvd_parse.c \
	mvd_game.c
endif

