SUBDIRS := gallium/auxiliary
AM_LDFLAGS = gallium/auxiliary/libgallium.la $(EPOXY_LIBS) $(GL_LIBS) -lgbm -lm -ldl -I./gallium/include

AM_CFLAGS = \
        -I$(top_srcdir)/src/gallium/drivers/virgl \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/include \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(LIBDRM_CFLAGS) \
	$(EPOXY_CFLAGS) \
	$(VISIBILITY_CFLAGS)

#graw_renderer_SOURCES = \
#        graw_pipe_renderer.c \
#        graw_renderer_glx.c

#graw_shm_renderer_SOURCES = \
#        graw_shm_renderer.c \
#        graw_renderer_glx.c \
#        send_scm.c

#graw_renderer_LDADD = libgrend.la -lX11
#graw_shm_renderer_LDADD = libgrend.la -lrt -lX11

libgrend_la_SOURCES = \
        graw_renderer.c \
        graw_shader.c \
        vrend_object.c \
        graw_decode.c \
	graw_cursor.c \
        graw_formats.c \
        iov.c \
        graw_renderer_helper.c \
	virgl_egl_context.c

lib_LTLIBRARIES = libvirglrenderer.la
noinst_LTLIBRARIES = libgrend.la

GM_LDFLAGS = -Wl,-Bsymbolic -version-number 0:1 -no-undefined

libvirglrenderer_la_SOURCES = graw_virtio_lib_renderer.c
libvirglrenderer_ladir = $(libdir)
libvirglrenderer_la_LIBADD = libgrend.la
libvirglrenderer_la_LDFLAGS = $(GM_LDFLAGS) $(EPOXY_LDFLAGS)

libvirglrendererincludedir = ${includedir}
libvirglrendererinclude_HEADERS = virglrenderer.h virgl_helper.h
