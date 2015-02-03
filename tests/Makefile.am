if BUILD_TESTS

AM_CPPFLAGS = -I$(top_srcdir)/src $(CHECK_CFLAGS)

TEST_LIBS = $(top_builddir)/src/libvirglrenderer.la $(CHECK_LIBS)

run_tests = test_virgl_init

noinst_PROGRAMS = $(run_tests)
TESTS = $(run_tests)

test_virgl_init_SOURCES = test_virgl_init.c
test_virgl_init_LDADD = $(TEST_LIBS)
test_virgl_init_LDFLAGS = -no-install

if HAVE_VALGRIND
VALGRIND_FLAGS=--leak-check=full \
	       --quiet \
	       --error-exitcode=3 \
		--suppressions=$(srcdir)/valgrind.suppressions

valgrind:
	$(MAKE) check-TESTS LOG_COMPILER="$(VALGRIND)" LOG_FLAGS="$(VALGRIND_FLAGS)" CK_FORK=no

check: valgrind

endif
endif
