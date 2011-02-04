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

CFLAGS+=-DUSE_REF=REF_SOFT

SRCFILES+=sw_aclip.c  \
	   sw_alias.c  \
	   sw_bsp.c    \
	   sw_draw.c   \
	   sw_edge.c   \
	   sw_image.c  \
	   sw_light.c  \
	   sw_main.c   \
	   sw_misc.c   \
	   sw_model.c  \
	   sw_part.c   \
	   sw_poly.c   \
	   sw_polyse.c \
	   sw_rast.c   \
	   sw_scan.c   \
	   sw_surf.c   \
	   sw_sird.c   \
	   sw_sky.c

ifdef USE_ASM
SRCFILES+=sw_protect.c
ASMFILES+=r_aclipa.s \
		 r_draw16.s \
		 r_drawa.s  \
		 r_edgea.s  \
		 r_scana.s  \
		 r_surf8.s  \
		 r_varsa.s  \
		 d_polysa.s \
		 fpu.s 
endif

ifdef MINGW
SRCFILES+=win_swimp.c
endif

