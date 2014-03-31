dnl Process this file with autoconf to create configure.

AC_PREREQ([2.60])

AC_INIT([virglrenderer], [0.0.1],
    [])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign dist-bzip2])

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])],
    [AC_SUBST([AM_DEFAULT_VERBOSITY], [1])])

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_SYS_LARGEFILE

XORG_MACROS_VERSION(1.8)
XORG_DEFAULT_OPTIONS

LIBDRM_REQUIRED=2.4.50

dnl Add flags for gcc
if test "x$GCC" = xyes; then
	CFLAGS="$CFLAGS -Wall -std=c99"
fi

DEFINES="-D_GNU_SOURCE"
AC_SUBST([DEFINES])
PKG_CHECK_MODULES([LIBDRM], [libdrm >= $LIBDRM_REQUIRED],
                  [have_libdrm=yes], [have_libdrm=no])

PKG_CHECK_MODULES([EPOXY], [epoxy],
                  [have_epoxy=yes], [have_epoxy=no])

AC_CONFIG_FILES([
		virglrenderer.pc
		Makefile
		src/Makefile
		src/gallium/auxiliary/Makefile
])
AC_OUTPUT

