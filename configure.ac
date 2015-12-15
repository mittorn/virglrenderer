dnl Process this file with autoconf to create configure.

AC_PREREQ([2.60])

AC_INIT([virglrenderer], [0.3.0],
    [])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
    [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_DISABLE_STATIC
LT_INIT([disable-static])
AC_SYS_LARGEFILE
AC_CHECK_PROGS([PYTHON2], [python2 python])
AX_CODE_COVERAGE

DEFINES="-D_GNU_SOURCE"

AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [use debug compiler flags and macros @<:@default=disabled@:>@])],
    [enable_debug="$enableval"],
    [enable_debug=no]
)
if test "x$enable_debug" = xyes; then
    DEFINES="$DEFINES -DDEBUG"
    if test "x$GCC" = xyes; then
        if ! echo "$CFLAGS" | grep -q -e '-g'; then
            CFLAGS="$CFLAGS -g"
        fi
        if ! echo "$CFLAGS" | grep -q -e '-O'; then
            CFLAGS="$CFLAGS -O0"
        fi
    fi
    if test "x$GXX" = xyes; then
        if ! echo "$CXXFLAGS" | grep -q -e '-g'; then
            CXXFLAGS="$CXXFLAGS -g"
        fi
        if ! echo "$CXXFLAGS" | grep -q -e '-O'; then
            CXXFLAGS="$CXXFLAGS -O0"
        fi
    fi
fi

AC_ARG_ENABLE(tests,
	      AS_HELP_STRING([--enable-tests], [Build the tests (default=auto)]),
	      [build_tests="$enableval"],
	      [build_tests="auto"])

PKG_CHECK_MODULES(CHECK, [check >= 0.9.4], [HAVE_CHECK="yes"], [HAVE_CHECK="no"])

if test "x$build_tests" = "xauto"; then
	build_tests="$HAVE_CHECK"
fi
if test "x$build_tests" = "xyes"; then
	if test "x$HAVE_CHECK" = "xno"; then
		AC_MSG_ERROR([Cannot build tests, check is missing])
	fi

	AC_PATH_PROG(VALGRIND, [valgrind])
fi

AM_CONDITIONAL(HAVE_VALGRIND, [test "x$VALGRIND" != "x"])
AM_CONDITIONAL(BUILD_TESTS, [test "x$build_tests" = "xyes"])

XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

LIBDRM_REQUIRED=2.4.50

dnl Add flags for gcc
if test "x$GCC" = xyes; then
	CFLAGS="$CFLAGS -Wall -std=c99"
fi

PKG_CHECK_MODULES([LIBDRM], [libdrm >= $LIBDRM_REQUIRED])
PKG_CHECK_MODULES([EPOXY], [epoxy])

AC_SUBST([DEFINES])
AC_CONFIG_FILES([
		virglrenderer.pc
		Makefile
		src/Makefile
		src/gallium/auxiliary/Makefile
		vtest/Makefile
		tests/Makefile
])
AC_OUTPUT
