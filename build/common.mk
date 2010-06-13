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

SRCFILES+=cmd.c \
	bsp.c \
	cmodel.c \
	common.c \
	prompt.c \
	crc.c \
	cvar.c \
	files.c \
	mdfour.c \
	net_common.c \
	net_chan.c \
	pmove.c \
	q_msg.c \
	q_shared.c \
	q_field.c \
	io_sleep.c

ifdef USE_ZLIB
LDFLAGS+=$(ZLIB_LDFLAGS)
CFLAGS+=$(ZLIB_CFLAGS)
endif

ifdef USE_ASM
ASMFILES+=math.s
endif

