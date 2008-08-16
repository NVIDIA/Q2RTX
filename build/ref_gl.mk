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

CFLAGS+=-DUSE_REF=REF_GL

SRCFILES+=gl_draw.c \
	   gl_images.c \
	   gl_models.c \
	   gl_world.c \
	   gl_mesh.c \
	   gl_main.c \
	   gl_state.c \
	   gl_surf.c \
	   gl_tess.c \
	   gl_sky.c \
	   qgl_api.c

ifdef MINGW
SRCFILES+=win_glimp.c win_wgl.c
endif

ifdef USE_JPG
LDFLAGS+=$(JPG_LDFLAGS)
CFLAGS+=$(JPG_CFLAGS)
endif

ifdef USE_PNG
LDFLAGS+=$(PNG_LDFLAGS)
CFLAGS+=$(PNG_CFLAGS)
endif

