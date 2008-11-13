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

TARGET=../q2proded$(EXESUFFIX)

CFLAGS+=-DUSE_CLIENT=0
#LDFLAGS+=-lm

include $(SRCDIR)/build/common.mk

SRCFILES+=cl_null.c

include $(SRCDIR)/build/server.mk

ifdef MINGW
SRCFILES+=sys_win.c
LDFLAGS+=-mconsole
ifdef WINCE
SRCFILES+=win_ascii.c
LDFLAGS+=-lwinsock -lmmtimer
else
LDFLAGS+=-lws2_32 -lwinmm -ladvapi32
endif
RESFILES+=q2proded.rc
else
SRCFILES+=sys_unix.c
ifdef USE_DL
LDFLAGS+=-ldl
endif
endif

include $(SRCDIR)/build/target.mk

