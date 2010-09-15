# Copyright (C) 2009 Andrey Nazarov
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

TARGET=../$(GAMELIB)

VPATH+=$(SRCDIR)/src/baseq2

CFLAGS+=-I$(SRCDIR)/src
LDFLAGS+=-lm -shared

ifdef MINGW
RESFILES+=baseq2.rc
OBJFILES+=baseq2.def
else
CFLAGS+=-fPIC
LDFLAGS+=-fPIC
endif

SRCFILES=q_shared.c \
g_ai.c      g_misc.c     g_trigger.c  m_boss31.c   m_float.c      m_medic.c      p_client.c \
g_chase.c   g_monster.c  g_turret.c   m_boss32.c   m_flyer.c      m_move.c       p_hud.c \
g_cmds.c    g_phys.c     g_utils.c    m_boss3.c    m_gladiator.c  m_mutant.c     p_trail.c \
g_combat.c  g_save.c     g_weapon.c   m_brain.c    m_gunner.c     m_parasite.c   p_view.c \
g_func.c    g_spawn.c    m_actor.c    m_chick.c    m_hover.c      m_soldier.c    p_weapon.c \
g_items.c   g_svcmds.c   m_berserk.c  m_flash.c    m_infantry.c   m_supertank.c \
g_main.c    g_target.c   m_boss2.c    m_flipper.c  m_insane.c     m_tank.c \
g_ptrs.c

include $(SRCDIR)/build/target.mk

