gcc -c dispatch_common.c dispatch_egl.c egl_generated_dispatch.c gl_generated_dispatch.c -I../include -I..
ar rcs ../../epoxy.a dispatch_common.o dispatch_egl.o egl_generated_dispatch.o gl_generated_dispatch.o