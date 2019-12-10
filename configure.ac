dnl Process this file with autoconf to create configure.

AC_PREREQ([2.60])

AC_INIT([virglrenderer], [0.8.0],
    [virglrenderer-devel@lists.freedesktop.org])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([build-aux])
PKG_PROG_PKG_CONFIG

AM_INIT_AUTOMAKE([foreign dist-bzip2])

AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC_C99

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
    [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_DISABLE_STATIC
LT_INIT([disable-static])
AC_SYS_LARGEFILE
AC_CHECK_PROGS([PYTHON], [python2 python python3])
AX_CODE_COVERAGE

AX_CHECK_COMPILE_FLAG([-Wall],                                 [CFLAGS="$CFLAGS -Wall"])
AX_CHECK_COMPILE_FLAG([-Wextra],                               [CFLAGS="$CFLAGS -Wextra"])
AX_CHECK_COMPILE_FLAG([-Werror=implicit-function-declaration], [CFLAGS="$CFLAGS -Werror=implicit-function-declaration"])
AX_CHECK_COMPILE_FLAG([-Werror=missing-prototypes],            [CFLAGS="$CFLAGS -Werror=missing-prototypes"])
AX_CHECK_COMPILE_FLAG([-Wmissing-prototypes],                  [CFLAGS="$CFLAGS -Wmissing-prototypes"])
AX_CHECK_COMPILE_FLAG([-Werror=int-to-pointer-cast],           [CFLAGS="$CFLAGS -Werror=int-to-pointer-cast"])

AX_GCC_FUNC_ATTRIBUTE([visibility])
AX_CHECK_COMPILE_FLAG([-fvisibility=hidden],                   [VISIBILITY_CFLAGS="-fvisibility=hidden"])


AC_MSG_CHECKING([for native Win32])
case "$host_os" in
     *mingw*|*cygwin*)
        os_win32=yes
        ;;
     *)
        os_win32=no
        ;;
esac
AC_MSG_RESULT([$os_win32])
AM_CONDITIONAL([OS_WIN32],[test "$os_win32" = "yes"])

if test "x$os_win32" = xno; then
   AX_PTHREAD
fi

AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
        [use debug compiler flags and macros @<:@default=disabled@:>@])],
    [enable_debug="$enableval"],
    [enable_debug=no]
)

AC_ARG_ENABLE([gbm-allocation],
    [AS_HELP_STRING([--enable-gbm-allocation], [Enable GBM allocation])],
    [enable_gbm_allocation="$enableval"],
    [enable_gbm_allocation=no]
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

AC_ARG_ENABLE(valgrind,
	      AS_HELP_STRING([--enable-valgrind], [Run the test suite also in valgind]),
	      [test_valgrind="$enableval"], [test_valgrind="no"])

AC_ARG_ENABLE([egl], AS_HELP_STRING([--disable-egl], [Disable EGL]))
AC_ARG_ENABLE([glx], AS_HELP_STRING([--disable-glx], [Disable GLX]))

PKG_CHECK_MODULES(CHECK, [check >= 0.9.4], [HAVE_CHECK="yes"], [HAVE_CHECK="no"])

if test "x$build_tests" = "xauto" && test "x$os_win32" = "xno"; then
	build_tests="$HAVE_CHECK"
fi

if test "x$build_tests" = "xyes"; then
	if test "x$HAVE_CHECK" = "xno"; then
		AC_MSG_ERROR([Cannot build tests, check is missing])
	fi

	if test "x$test_valgrind" = "xyes"; then
		AC_PATH_PROG(VALGRIND, [valgrind])
                if test "x$VALGRIND" = "x"; then
                        enable_valgrind="no"
                        AC_MSG_ERROR([Cannot run test with valgrind])
                else
                        enable_valgrind="yes"
                fi

	fi
fi

AC_ARG_ENABLE(fuzzer,
	      AS_HELP_STRING([--enable-fuzzer], [Build fuzzer targets]),
	      [enable_fuzzer="$enableval"],
	      [enable_fuzzer=no]
)
AM_CONDITIONAL(FUZZER, [test "x$enable_fuzzer" = "xyes"])

AC_CHECK_FUNCS_ONCE([eventfd])
AC_CHECK_HEADERS_ONCE([sys/uio.h])
AM_CONDITIONAL(HAVE_VALGRIND, [test "x$VALGRIND" != "x"])
AM_CONDITIONAL(BUILD_TESTS, [test "x$build_tests" = "xyes"])

LIBDRM_REQUIRED=2.4.50

if test "x$enable_gbm_allocation" = xyes; then
	LIBGBM_REQUIRED=18.0.0
else
	LIBGBM_REQUIRED=0.0.0
fi

if test "x$os_win32" = xno && test "x$enable_egl" != "xno"; then
	PKG_CHECK_MODULES([LIBDRM], [libdrm >= $LIBDRM_REQUIRED])
	PKG_CHECK_MODULES([GBM], [gbm >= $LIBGBM_REQUIRED])
fi

LIBEPOXY_REQURED=1.5.4
PKG_CHECK_MODULES([EPOXY], [epoxy >= $LIBEPOXY_REQURED])
epoxy_has_egl=`$PKG_CONFIG --variable=epoxy_has_egl epoxy`
AS_IF([test "x$epoxy_has_egl" = "x1" && test "x$enable_egl" != "xno"], [
  epoxy_has_egl=yes
  AC_DEFINE([HAVE_EPOXY_EGL_H], [1], [Libepoxy has EGL support.])
],[
  epoxy_has_egl=no
])
AM_CONDITIONAL([HAVE_EPOXY_EGL], [test "x$epoxy_has_egl" = "xyes"])

if test "x$enable_gbm_allocation" = xyes; then
	if test "x$enable_egl" = xno; then
		AC_MSG_ERROR([GBM integration requires EGL support])
	fi
	AC_DEFINE([ENABLE_GBM_ALLOCATION], [1], [GBM will be used to allocate in some cases])
fi

epoxy_has_glx=`$PKG_CONFIG --variable=epoxy_has_glx epoxy`
AS_IF([test "x$epoxy_has_glx" = "x1" && test "x$enable_glx" != "xno"], [
  PKG_CHECK_MODULES([X11], [x11])
  epoxy_has_glx=yes
  AC_DEFINE([HAVE_EPOXY_GLX_H], [1], [Libepoxy has GLX support.])
],[
  epoxy_has_glx=no
])
AM_CONDITIONAL([HAVE_EPOXY_GLX], [test "x$epoxy_has_glx" = "xyes"])

AC_SUBST([DEFINES])

case "$host_os" in
cygwin*)
    VISIBILITY_CFLAGS=""
    ;;
esac

AC_SUBST([VISIBILITY_CFLAGS])

AC_CONFIG_FILES([
		virglrenderer.pc
		Makefile
		src/Makefile
		src/gallium/auxiliary/Makefile
		vtest/Makefile
		tests/Makefile
		tests/fuzzer/Makefile
])
AC_OUTPUT

AC_MSG_NOTICE([

      virgl $VERSION
      ==============

      prefix:                   $prefix
      c compiler:               $CC
      win32:                    $os_win32

      glx:                      $epoxy_has_glx
      egl:                      $epoxy_has_egl
      gbm_allocation:           $enable_gbm_allocation
      debug:                    $enable_debug
      tests:                    $build_tests
      valgrind:                 $enable_valgrind
      fuzzer:                   $enable_fuzzer

])
