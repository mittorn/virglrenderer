
AM_LDFLAGS = $(X11_LIBS)
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

bin_PROGRAMS = virgl_test_server

virgl_test_server_SOURCES =			\
	util.c					\
	util.h					\
	vtest_shm.c				\
	vtest_shm.h				\
	vtest_server.c				\
	vtest_renderer.c			\
	vtest_protocol.h			\
	vtest.h

virgl_test_server_LDADD = $(top_builddir)/src/gallium/auxiliary/libgallium.la $(top_builddir)/src/libvirglrenderer.la

if FUZZER
noinst_PROGRAMS = vtest_fuzzer

vtest_fuzzer_SOURCES =                          \
	util.c					\
	vtest_fuzzer.c				\
	vtest_renderer.c                        \
	vtest_shm.c

vtest_fuzzer_CFLAGS = \
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
	$(CODE_COVERAGE_CFLAGS) \
	-fsanitize=fuzzer

vtest_fuzzer_LDADD = $(top_builddir)/src/gallium/auxiliary/libgallium.la $(top_builddir)/src/libvirglrenderer.la
endif
