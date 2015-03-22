AM_CFLAGS = \
        -I$(top_srcdir)/src/gallium/drivers/virgl \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(LIBDRM_CFLAGS) \
	$(EPOXY_CFLAGS) \
	$(VISIBILITY_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)

noinst_PROGRAMS = vtest_server

vtest_server_SOURCES = \
	vtest_server.c \
	vtest_renderer.c \
	vtest_protocol.h \
	vtest.h

vtest_server_LDADD = $(top_builddir)/src/libvirglrenderer.la
