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
#ifndef VDIRECT_EGL_H
#define VDIRECT_EGL_H
#include "virglrenderer.h"
#include <epoxy/egl.h>

struct vdirect_egl;

bool vdirect_egl_init(struct vdirect_renderer *renderer, bool surfaceless, bool gles);

void vdirect_egl_destroy(struct vdirect_renderer *renderer);

virgl_renderer_gl_context vdirect_egl_create_context(struct vdirect_renderer *renderer, int scanout_idx,
                                                   struct virgl_renderer_gl_ctx_param *vparams);

void vdirect_egl_destroy_context(struct vdirect_renderer *renderer, virgl_renderer_gl_context virglctx);

int vdirect_egl_make_context_current(struct vdirect_renderer *renderer, int scanout_idx, virgl_renderer_gl_context virglctx);

virgl_renderer_gl_context vdirect_egl_get_current_context(struct vdirect_renderer *renderer);

bool virgl_has_egl_khr_gl_colorspace(struct vdirect_renderer *renderer);

int vdirect_egl_get_fourcc_for_texture(struct vdirect_renderer *renderer, uint32_t tex_id, uint32_t format,
                                     int *fourcc);

int vdirect_egl_get_fd_for_texture(struct vdirect_renderer *renderer, uint32_t tex_id, int *fd);

int vdirect_egl_get_fd_for_texture2(struct vdirect_renderer *renderer, uint32_t tex_id, int *fd, int *stride,
                                  int *offset);

void *vdirect_egl_image_from_dmabuf(struct vdirect_renderer *renderer,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t drm_format,
                                  uint64_t drm_modifier,
                                  uint32_t plane_count,
                                  const int *plane_fds,
                                  const uint32_t *plane_strides,
                                  const uint32_t *plane_offsets);
void vdirect_egl_image_destroy(struct vdirect_renderer *renderer, void *image);

void *vdirect_egl_image_from_gbm_bo(struct vdirect_renderer *renderer, struct gbm_bo *bo);
void *vdirect_egl_aux_plane_image_from_gbm_bo(struct vdirect_renderer *renderer, struct gbm_bo *bo, int plane);

bool vdirect_egl_supports_fences(struct vdirect_renderer *renderer);
EGLSyncKHR vdirect_egl_fence_create(struct vdirect_renderer *renderer);
void vdirect_egl_fence_destroy(struct vdirect_renderer *renderer, EGLSyncKHR fence);
bool vdirect_egl_client_wait_fence(struct vdirect_renderer *renderer, EGLSyncKHR fence, uint64_t timeout);
bool vdirect_egl_export_signaled_fence(struct vdirect_renderer *renderer, int *out_fd);
bool vdirect_egl_export_fence(struct vdirect_renderer *renderer, EGLSyncKHR fence, int *out_fd);
#endif
