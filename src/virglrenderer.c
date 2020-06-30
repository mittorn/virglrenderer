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

#include <stdio.h>
#include <time.h>

#include <epoxy/gl.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "vrend_renderer.h"

#include "virglrenderer.h"

#include "virgl_context.h"
#include "virgl_resource.h"

#ifdef HAVE_EPOXY_EGL_H
#include "virgl_gbm.h"
#include "virgl_egl.h"
#endif

#ifdef HAVE_EPOXY_GLX_H
#include "virgl_glx.h"
static struct virgl_glx *glx_info;
#endif

/* new API - just wrap internal API for now */

static int virgl_renderer_resource_create_internal(struct virgl_renderer_resource_create_args *args,
                                                   UNUSED struct iovec *iov, UNUSED uint32_t num_iovs,
                                                   void *image)
{
   int ret;
   struct pipe_resource *pipe_res;
   struct vrend_renderer_resource_create_args vrend_args =  { 0 };

   /* do not accept handle 0 */
   if (args->handle == 0)
      return EINVAL;

   vrend_args.target = args->target;
   vrend_args.format = args->format;
   vrend_args.bind = args->bind;
   vrend_args.width = args->width;
   vrend_args.height = args->height;
   vrend_args.depth = args->depth;
   vrend_args.array_size = args->array_size;
   vrend_args.nr_samples = args->nr_samples;
   vrend_args.last_level = args->last_level;
   vrend_args.flags = args->flags;

   pipe_res = vrend_renderer_resource_create(&vrend_args, image);
   if (!pipe_res)
      return EINVAL;

   ret = virgl_resource_create_from_pipe(args->handle, pipe_res);
   if (ret) {
      vrend_renderer_resource_destroy((struct vrend_resource *)pipe_res);
      return ret;
   }

   if (!ret && num_iovs) {
      ret = virgl_renderer_resource_attach_iov(args->handle, iov, num_iovs);
      if (ret) {
         virgl_resource_remove(args->handle);
         return ret;
      }
   }

   return 0;
}

int virgl_renderer_resource_create(struct virgl_renderer_resource_create_args *args,
                                   struct iovec *iov, uint32_t num_iovs)
{
   return virgl_renderer_resource_create_internal(args, iov, num_iovs, NULL);
}

int virgl_renderer_resource_import_eglimage(struct virgl_renderer_resource_create_args *args, void *image)
{
#ifdef HAVE_EPOXY_EGL_H
   return virgl_renderer_resource_create_internal(args, NULL, 0, image);
#else
   return EINVAL;
#endif
}

void virgl_renderer_resource_set_priv(uint32_t res_handle, void *priv)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res)
      return;

   res->private_data = priv;
}

void *virgl_renderer_resource_get_priv(uint32_t res_handle)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res)
      return NULL;

   return res->private_data;
}

static bool detach_resource(struct virgl_context *ctx, void *data)
{
   struct virgl_resource *res = data;
   ctx->detach_resource(ctx, res);
   return true;
}

void virgl_renderer_resource_unref(uint32_t res_handle)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   struct virgl_context_foreach_args args;

   if (!res)
      return;

   args.callback = detach_resource;
   args.data = res;
   virgl_context_foreach(&args);

   virgl_resource_remove(res->res_id);
}

void virgl_renderer_fill_caps(uint32_t set, uint32_t version,
                              void *caps)
{
   switch (set) {
   case VIRGL_CAPSET_VIRGL:
   case VIRGL_CAPSET_VIRGL2:
      vrend_renderer_fill_caps(set, version, (union virgl_caps *)caps);
      break;
   default:
      break;
   }
}

int virgl_renderer_context_create(uint32_t handle, uint32_t nlen, const char *name)
{
   struct virgl_context *ctx;
   int ret;

   /* user context id must be greater than 0 */
   if (handle == 0)
      return EINVAL;

   if (virgl_context_lookup(handle))
      return 0;

   ctx = vrend_renderer_context_create(handle, nlen, name);
   if (!ctx)
      return ENOMEM;

   ret = virgl_context_add(ctx);
   if (ret) {
      ctx->destroy(ctx);
      return ret;
   }

   return 0;
}

void virgl_renderer_context_destroy(uint32_t handle)
{
   virgl_context_remove(handle);
}

int virgl_renderer_submit_cmd(void *buffer,
                              int ctx_id,
                              int ndw)
{
   struct virgl_context *ctx = virgl_context_lookup(ctx_id);
   if (!ctx)
      return EINVAL;
   return ctx->submit_cmd(ctx, buffer, sizeof(uint32_t) * ndw);
}

int virgl_renderer_transfer_write_iov(uint32_t handle,
                                      uint32_t ctx_id,
                                      int level,
                                      uint32_t stride,
                                      uint32_t layer_stride,
                                      struct virgl_box *box,
                                      uint64_t offset,
                                      struct iovec *iovec,
                                      unsigned int iovec_cnt)
{
   struct virgl_resource *res = virgl_resource_lookup(handle);
   struct vrend_transfer_info transfer_info;

   if (!res)
      return EINVAL;

   transfer_info.level = level;
   transfer_info.stride = stride;
   transfer_info.layer_stride = layer_stride;
   transfer_info.box = (struct pipe_box *)box;
   transfer_info.offset = offset;
   transfer_info.iovec = iovec;
   transfer_info.iovec_cnt = iovec_cnt;
   transfer_info.synchronized = false;

   if (ctx_id) {
      struct virgl_context *ctx = virgl_context_lookup(ctx_id);
      if (!ctx)
         return EINVAL;

      return ctx->transfer_3d(ctx, res, &transfer_info,
                              VIRGL_TRANSFER_TO_HOST);
   } else {
      if (!res->pipe_resource)
         return EINVAL;

      return vrend_renderer_transfer_pipe(res->pipe_resource, &transfer_info,
                                          VIRGL_TRANSFER_TO_HOST);
   }
}

int virgl_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct virgl_box *box,
                                     uint64_t offset, struct iovec *iovec,
                                     int iovec_cnt)
{
   struct virgl_resource *res = virgl_resource_lookup(handle);
   struct vrend_transfer_info transfer_info;

   if (!res)
      return EINVAL;

   transfer_info.level = level;
   transfer_info.stride = stride;
   transfer_info.layer_stride = layer_stride;
   transfer_info.box = (struct pipe_box *)box;
   transfer_info.offset = offset;
   transfer_info.iovec = iovec;
   transfer_info.iovec_cnt = iovec_cnt;
   transfer_info.synchronized = false;

   if (ctx_id) {
      struct virgl_context *ctx = virgl_context_lookup(ctx_id);
      if (!ctx)
         return EINVAL;

      return ctx->transfer_3d(ctx, res, &transfer_info,
                              VIRGL_TRANSFER_FROM_HOST);
   } else {
      if (!res->pipe_resource)
         return EINVAL;

      return vrend_renderer_transfer_pipe(res->pipe_resource, &transfer_info,
                                          VIRGL_TRANSFER_FROM_HOST);
   }
}

int virgl_renderer_resource_attach_iov(int res_handle, struct iovec *iov,
                                       int num_iovs)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res)
      return EINVAL;

   return virgl_resource_attach_iov(res, iov, num_iovs);
}

void virgl_renderer_resource_detach_iov(int res_handle, struct iovec **iov_p, int *num_iovs_p)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res)
      return;

   if (iov_p)
      *iov_p = (struct iovec *)res->iov;
   if (num_iovs_p)
      *num_iovs_p = res->iov_count;

   virgl_resource_detach_iov(res);
}

int virgl_renderer_create_fence(int client_fence_id, uint32_t ctx_id)
{
   return vrend_renderer_create_fence(client_fence_id, ctx_id);
}

void virgl_renderer_force_ctx_0(void)
{
   vrend_renderer_force_ctx_0();
}

void virgl_renderer_ctx_attach_resource(int ctx_id, int res_handle)
{
   struct virgl_context *ctx = virgl_context_lookup(ctx_id);
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!ctx || !res)
      return;
   ctx->attach_resource(ctx, res);
}

void virgl_renderer_ctx_detach_resource(int ctx_id, int res_handle)
{
   struct virgl_context *ctx = virgl_context_lookup(ctx_id);
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!ctx || !res)
      return;
   ctx->detach_resource(ctx, res);
}

int virgl_renderer_resource_get_info(int res_handle,
                                     struct virgl_renderer_resource_info *info)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   int ret;

   if (!res || !res->pipe_resource)
      return EINVAL;
   if (!info)
      return EINVAL;

   ret = vrend_renderer_resource_get_info(res->pipe_resource,
                                          (struct vrend_renderer_resource_info *)info);
   info->handle = res_handle;
#ifdef HAVE_EPOXY_EGL_H
   if (ret == 0 && use_context == CONTEXT_EGL)
      return virgl_egl_get_fourcc_for_texture(egl, info->tex_id, info->virgl_format, &info->drm_fourcc);
#endif

   return ret;
}

void virgl_renderer_get_cap_set(uint32_t cap_set, uint32_t *max_ver,
                                uint32_t *max_size)
{
   switch (cap_set) {
   case VIRGL_CAPSET_VIRGL:
   case VIRGL_CAPSET_VIRGL2:
      vrend_renderer_get_cap_set(cap_set, max_ver, max_size);
      break;
   default:
      *max_ver = 0;
      *max_size = 0;
      break;
   }
}

void virgl_renderer_get_rect(int resource_id, struct iovec *iov, unsigned int num_iovs,
                             uint32_t offset, int x, int y, int width, int height)
{
   struct virgl_resource *res = virgl_resource_lookup(resource_id);
   if (!res || !res->pipe_resource)
      return;

   vrend_renderer_get_rect(res->pipe_resource, iov, num_iovs, offset, x, y,
                           width, height);
}


static struct virgl_renderer_callbacks *rcbs;

static void *dev_cookie;

static struct vrend_if_cbs virgl_cbs;

static void virgl_write_fence(uint32_t fence_id)
{
   rcbs->write_fence(dev_cookie, fence_id);
}

static virgl_renderer_gl_context create_gl_context(int scanout_idx, struct virgl_gl_ctx_param *param)
{
   struct virgl_renderer_gl_ctx_param vparam;

#ifdef HAVE_EPOXY_EGL_H
   if (use_context == CONTEXT_EGL)
      return virgl_egl_create_context(egl, param);
#endif
#ifdef HAVE_EPOXY_GLX_H
   if (use_context == CONTEXT_GLX)
      return virgl_glx_create_context(glx_info, param);
#endif
   vparam.version = 1;
   vparam.shared = param->shared;
   vparam.major_ver = param->major_ver;
   vparam.minor_ver = param->minor_ver;
   return rcbs->create_gl_context(dev_cookie, scanout_idx, &vparam);
}

static void destroy_gl_context(virgl_renderer_gl_context ctx)
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
   rcbs->destroy_gl_context(dev_cookie, ctx);
}

static int make_current(virgl_renderer_gl_context ctx)
{
#ifdef HAVE_EPOXY_EGL_H
   if (use_context == CONTEXT_EGL)
      return virgl_egl_make_context_current(egl, ctx);
#endif
#ifdef HAVE_EPOXY_GLX_H
   if (use_context == CONTEXT_GLX)
      return virgl_glx_make_context_current(glx_info, ctx);
#endif
   return rcbs->make_current(dev_cookie, 0, ctx);
}

static struct vrend_if_cbs virgl_cbs = {
   virgl_write_fence,
   create_gl_context,
   destroy_gl_context,
   make_current,
};

void *virgl_renderer_get_cursor_data(uint32_t resource_id, uint32_t *width, uint32_t *height)
{
   struct virgl_resource *res = virgl_resource_lookup(resource_id);
   if (!res || !res->pipe_resource)
      return NULL;

   vrend_renderer_force_ctx_0();
   return vrend_renderer_get_cursor_contents(res->pipe_resource,
                                             width,
                                             height);
}

void virgl_renderer_poll(void)
{
   vrend_renderer_check_fences();
}

void virgl_renderer_cleanup(UNUSED void *cookie)
{
   vrend_renderer_fini();
   virgl_context_table_cleanup();

#ifdef HAVE_EPOXY_EGL_H
   if (use_context == CONTEXT_EGL) {
      virgl_egl_destroy(egl);
      egl = NULL;
      use_context = CONTEXT_NONE;
      if (gbm) {
         virgl_gbm_fini(gbm);
         gbm = NULL;
      }
   }
#endif
#ifdef HAVE_EPOXY_GLX_H
   if (use_context == CONTEXT_GLX) {
      virgl_glx_destroy(glx_info);
      glx_info = NULL;
      use_context = CONTEXT_NONE;
   }
#endif
}

int virgl_renderer_init(void *cookie, int flags, struct virgl_renderer_callbacks *cbs)
{
   uint32_t renderer_flags = 0;
   if (!cookie || !cbs)
      return -1;

   if (cbs->version < 1 || cbs->version > VIRGL_RENDERER_CALLBACKS_VERSION)
      return -1;

   dev_cookie = cookie;
   rcbs = cbs;

   if (flags & VIRGL_RENDERER_USE_EGL) {
#ifdef HAVE_EPOXY_EGL_H
      int fd = -1;
      if (cbs->version >= 2 && cbs->get_drm_fd) {
         fd = cbs->get_drm_fd(cookie);
      }

      /*
       * If the user specifies a preferred DRM fd and we can't use it, fail. If the user doesn't
       * specify an fd, it's possible to initialize EGL without one.
       */
      gbm = virgl_gbm_init(fd);
      if (fd > 0 && !gbm)
         return -1;

      egl = virgl_egl_init(gbm, flags & VIRGL_RENDERER_USE_SURFACELESS,
                           flags & VIRGL_RENDERER_USE_GLES);
      if (!egl) {
         if (gbm) {
            virgl_gbm_fini(gbm);
            gbm = NULL;
         }

         return -1;
      }

      use_context = CONTEXT_EGL;
#else
      vrend_printf( "EGL is not supported on this platform\n");
      return -1;
#endif
   } else if (flags & VIRGL_RENDERER_USE_GLX) {
#ifdef HAVE_EPOXY_GLX_H
      glx_info = virgl_glx_init();
      if (!glx_info)
         return -1;
      use_context = CONTEXT_GLX;
#else
      vrend_printf( "GLX is not supported on this platform\n");
      return -1;
#endif
   }

   if (virgl_context_table_init())
      return -1;

   if (flags & VIRGL_RENDERER_THREAD_SYNC)
      renderer_flags |= VREND_USE_THREAD_SYNC;
   if (flags & VIRGL_RENDERER_USE_EXTERNAL_BLOB)
      renderer_flags |= VREND_USE_EXTERNAL_BLOB;

   return vrend_renderer_init(&virgl_cbs, renderer_flags);
}

int virgl_renderer_get_fd_for_texture(uint32_t tex_id, int *fd)
{
#ifdef HAVE_EPOXY_EGL_H
   if (!egl)
      return -1;

   return virgl_egl_get_fd_for_texture(egl, tex_id, fd);
#else
   return -1;
#endif
}

int virgl_renderer_get_fd_for_texture2(uint32_t tex_id, int *fd, int *stride, int *offset)
{
#ifdef HAVE_EPOXY_EGL_H
   if (!egl)
      return -1;

   return virgl_egl_get_fd_for_texture2(egl, tex_id, fd, stride, offset);
#else
   return -1;
#endif
}

void virgl_renderer_reset(void)
{
   vrend_renderer_reset();
}

int virgl_renderer_get_poll_fd(void)
{
   return vrend_renderer_get_poll_fd();
}

virgl_debug_callback_type virgl_set_debug_callback(virgl_debug_callback_type cb)
{
   return vrend_set_debug_callback(cb);
}

static int virgl_renderer_export_query(void *execute_args, uint32_t execute_size)
{
   struct virgl_resource *res;
   struct virgl_renderer_export_query *export_query = execute_args;
   if (execute_size != sizeof(struct virgl_renderer_export_query))
      return -EINVAL;

   if (export_query->hdr.size != sizeof(struct virgl_renderer_export_query))
      return -EINVAL;

   res = virgl_resource_lookup(export_query->in_resource_id);
   if (!res || !res->pipe_resource)
      return -EINVAL;

   return vrend_renderer_export_query(res->pipe_resource, export_query);
}

static int virgl_renderer_supported_structures(void *execute_args, uint32_t execute_size)
{
   struct virgl_renderer_supported_structures *supported_structures = execute_args;
   if (execute_size != sizeof(struct virgl_renderer_supported_structures))
      return -EINVAL;

   if (supported_structures->hdr.size != sizeof(struct virgl_renderer_supported_structures))
      return -EINVAL;

   if (supported_structures->in_stype_version == 0) {
      supported_structures->out_supported_structures_mask =
         VIRGL_RENDERER_STRUCTURE_TYPE_EXPORT_QUERY |
         VIRGL_RENDERER_STRUCTURE_TYPE_SUPPORTED_STRUCTURES;
   } else {
      supported_structures->out_supported_structures_mask = 0;
   }

   return 0;
}

int virgl_renderer_execute(void *execute_args, uint32_t execute_size)
{
   struct virgl_renderer_hdr *hdr = execute_args;
   if (hdr->stype_version != 0)
      return -EINVAL;

   switch (hdr->stype) {
      case VIRGL_RENDERER_STRUCTURE_TYPE_SUPPORTED_STRUCTURES:
         return virgl_renderer_supported_structures(execute_args, execute_size);
      case VIRGL_RENDERER_STRUCTURE_TYPE_EXPORT_QUERY:
         return virgl_renderer_export_query(execute_args, execute_size);
      default:
         return -EINVAL;
   }
}

int virgl_renderer_resource_create_blob(const struct virgl_renderer_resource_create_blob_args *args)
{

   int ret;
   uint32_t blob_mem = args->blob_mem;
   uint64_t blob_id = args->blob_id;
   uint32_t res_handle = args->res_handle;
   struct pipe_resource *pipe_res;

   if (blob_mem == VIRGL_RENDERER_BLOB_MEM_HOST3D ||
       blob_mem == VIRGL_RENDERER_BLOB_MEM_HOST3D_GUEST) {
      struct virgl_context *ctx = virgl_context_lookup(args->ctx_id);
      if (!ctx)
         return -EINVAL;

      pipe_res = ctx->get_blob_pipe(ctx, blob_id);
      if (!pipe_res)
         return -EINVAL;

      ret = virgl_resource_create_from_pipe(res_handle, pipe_res);
      if (ret) {
         vrend_renderer_resource_destroy((struct vrend_resource *)pipe_res);
         return ret;
      }

      if (blob_mem == VIRGL_RENDERER_BLOB_MEM_HOST3D_GUEST) {
         ret = virgl_renderer_resource_attach_iov(res_handle, args->iovecs, args->num_iovs);
         if (ret) {
            virgl_resource_remove(res_handle);
            return ret;
         }
      }
   } else if (blob_mem == VIRGL_RENDERER_BLOB_MEM_GUEST) {
      ret = virgl_resource_create_from_iov(res_handle, args->iovecs, args->num_iovs);
      if (ret)
         return -EINVAL;
   } else {
      return -EINVAL;
   }


   return 0;
}

int virgl_renderer_resource_map(uint32_t res_handle, void **map, uint64_t *out_size)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res || !res->pipe_resource)
      return -EINVAL;

   return vrend_renderer_resource_map(res->pipe_resource, map, out_size);
}

int virgl_renderer_resource_unmap(uint32_t res_handle)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res || !res->pipe_resource)
      return -EINVAL;

   return vrend_renderer_resource_unmap(res->pipe_resource);
}

int virgl_renderer_resource_get_map_info(uint32_t res_handle, uint32_t *map_info)
{
   struct virgl_resource *res = virgl_resource_lookup(res_handle);
   if (!res || !res->pipe_resource)
      return -EINVAL;

   return vrend_renderer_resource_get_map_info(res->pipe_resource, map_info);
}
