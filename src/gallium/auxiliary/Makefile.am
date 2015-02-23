AUTOMAKE_OPTIONS = subdir-objects

include Makefile.sources
include $(top_srcdir)/src/gallium/Automake.inc

noinst_LTLIBRARIES = libgallium.la

AM_CFLAGS = \
	-I$(top_srcdir)/src/gallium/auxiliary/util \
	-I$(top_srcdir)/src/gallium/auxiliary/ \
	$(GALLIUM_CFLAGS) \
	$(VISIBILITY_CFLAGS)

AM_CXXFLAGS = $(VISIBILITY_CXXFLAGS)

libgallium_la_SOURCES = \
	$(C_SOURCES) \
	$(GENERATED_SOURCES)


-include $(top_srcdir)/git.mk
