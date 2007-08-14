# -----------------------------
# q2pro makefile by [SkulleR]
# -----------------------------

include ../config.mk

TARGET=$(OUTDIR)/ref_gl$(LIBSUFFIX)

LDFLAGS+=-lm -shared
CFLAGS+=-DOPENGL_RENDERER=1 -DTRUECOLOR_RENDERER=1

ifdef USE_JPEG
LDFLAGS+=$(JPEG_LDFLAGS)
CFLAGS+=$(JPEG_CFLAGS)
endif

ifdef USE_PNG
LDFLAGS+=$(PNG_LDFLAGS)
CFLAGS+=$(PNG_CFLAGS)
endif

ifdef MINGW
#OBJFILES+=ref_gl.def
RESFILES+=ref_gl.rc
else
CFLAGS+=-fPIC
LDFLAGS+=-fPIC
endif

SRCFILES=q_shared.c \
	   r_images.c  \
	   r_bsp.c \
	   gl_draw.c   \
	   gl_images.c  \
	   gl_models.c \
	   gl_world.c \
	   gl_mesh.c \
	   gl_main.c  \
	   gl_state.c  \
	   gl_surf.c  \
	   gl_tess.c   \
	   gl_sky.c   \
	   qgl_api.c

include $(SRCDIR)/build/target.mk

