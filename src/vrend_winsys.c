/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vrend_winsys.h"

#include <stddef.h>

int use_context = CONTEXT_NONE;

#ifdef HAVE_EPOXY_EGL_H
struct virgl_egl *egl = NULL;
struct virgl_gbm *gbm = NULL;
#endif

#ifdef HAVE_EPOXY_GLX_H
struct virgl_glx *glx_info = NULL;
#endif

virgl_renderer_gl_context vrend_winsys_create_context(struct virgl_gl_ctx_param *param)
{
#ifdef HAVE_EPOXY_EGL_H
   if (use_context == CONTEXT_EGL)
      return virgl_egl_create_context(egl, param);
#endif
#ifdef HAVE_EPOXY_GLX_H
   if (use_context == CONTEXT_GLX)
      return virgl_glx_create_context(glx_info, param);
#endif
   return NULL;
}

void vrend_winsys_destroy_context(virgl_renderer_gl_context ctx)
{
#ifdef HAVE_EPOXY_EGL_H
   if (use_context == CONTEXT_EGL) {
      virgl_egl_destroy_context(egl, ctx);
      return;
   }
#endif
#ifdef HAVE_EPOXY_GLX_H
   if (use_context == CONTEXT_GLX) {
      virgl_glx_destroy_context(glx_info, ctx);
      return;
   }
#endif
}

int vrend_winsys_make_context_current(virgl_renderer_gl_context ctx)
{
#ifdef HAVE_EPOXY_EGL_H
   if (use_context == CONTEXT_EGL)
      return virgl_egl_make_context_current(egl, ctx);
#endif
#ifdef HAVE_EPOXY_GLX_H
   if (use_context == CONTEXT_GLX)
      return virgl_glx_make_context_current(glx_info, ctx);
#endif
   return -1;
}

int vrend_winsys_has_gl_colorspace(void)
{
   bool egl_colorspace = false;
#ifdef HAVE_EPOXY_EGL_H
   if (egl)
      egl_colorspace = virgl_has_egl_khr_gl_colorspace(egl);
#endif
   return use_context == CONTEXT_NONE ||
         use_context == CONTEXT_GLX ||
         (use_context == CONTEXT_EGL && egl_colorspace);
}
