# -----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

include ../config.mk

TARGET=$(OUTDIR)/ref_soft$(LIBSUFFIX)

LDFLAGS+=-lm -shared
CFLAGS+=-DSOFTWARE_RENDERER

SRCFILES=q_shared.c \
	   sw_aclip.c  \
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
	   sw_sprite.c \
	   sw_surf.c   \
	   sw_sird.c   \
	   r_images.c

ifdef USE_ASM
SRCFILES+=sw_protect.c
ASMFILES=r_aclipa.s \
		 r_draw16.s \
		 r_drawa.s  \
		 r_edgea.s  \
		 r_scana.s  \
		 r_spr8.s   \
		 r_surf8.s  \
		 r_varsa.s  \
		 d_polysa.s \
		 fpu.s
endif

ifdef MINGW
#OBJFILES+=ref_soft.def
RESFILES=ref_soft.rc
else
LDFLAGS+=-fPIC
CFLAGS+=-fPIC
endif

include $(SRCDIR)/build/target.mk

