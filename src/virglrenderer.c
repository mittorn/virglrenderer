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

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/eventfd.h>
#include "pipe/p_state.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "vrend_renderer.h"

#include "virglrenderer.h"
#include "virgl_egl.h"
/* new API - just wrap internal API for now */

int virgl_renderer_resource_create(struct virgl_renderer_resource_create_args *args, struct iovec *iov, uint32_t num_iovs)
{
   return vrend_renderer_resource_create((struct vrend_renderer_resource_create_args *)args, iov, num_iovs);
}

void virgl_renderer_resource_unref(uint32_t res_handle)
{
   vrend_renderer_resource_unref(res_handle);
}

void virgl_renderer_fill_caps(uint32_t set, uint32_t version,
                              void *caps)
{
  vrend_renderer_fill_caps(set, version, (union virgl_caps *)caps);
}

int virgl_renderer_context_create(uint32_t handle, uint32_t nlen, const char *name)
{
   vrend_renderer_context_create(handle, nlen, name);
   return 0;
}

void virgl_renderer_context_destroy(uint32_t handle)
{
   vrend_renderer_context_destroy(handle);
}

void virgl_renderer_submit_cmd(void *buffer,
                               int ctx_id,
                               int ndw)
{
   vrend_decode_block(ctx_id, buffer, ndw);
}

void virgl_renderer_transfer_write_iov(uint32_t handle, 
                                       uint32_t ctx_id,
                                       int level,
                                       uint32_t stride,
                                       uint32_t layer_stride,
                                       struct virgl_box *box,
                                       uint64_t offset,
                                       struct iovec *iovec,
                                       unsigned int iovec_cnt)
{
   vrend_renderer_transfer_write_iov(handle, ctx_id, level,
                                    stride, layer_stride, (struct pipe_box *)box,
                                    offset, iovec, iovec_cnt);
}

void virgl_renderer_transfer_read_iov(uint32_t handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct virgl_box *box,
                                     uint64_t offset, struct iovec *iov,
                                     int iovec_cnt)
{
   vrend_renderer_transfer_send_iov(handle, ctx_id, level, stride,
                                   layer_stride, (struct pipe_box *)box,
                                   offset, iov, iovec_cnt);
}

int virgl_renderer_resource_attach_iov(int res_handle, struct iovec *iov,
                                      int num_iovs)
{
   return vrend_renderer_resource_attach_iov(res_handle, iov, num_iovs);
}

void virgl_renderer_resource_detach_iov(int res_handle, struct iovec **iov_p, int *num_iovs_p)
{
   return vrend_renderer_resource_detach_iov(res_handle, iov_p, num_iovs_p);
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
   vrend_renderer_attach_res_ctx(ctx_id, res_handle);
}

void virgl_renderer_ctx_detach_resource(int ctx_id, int res_handle)
{
   vrend_renderer_detach_res_ctx(ctx_id, res_handle);
}

int virgl_renderer_resource_get_info(int res_handle,
                                     struct virgl_renderer_resource_info *info)
{
   int ret;
   ret = vrend_renderer_resource_get_info(res_handle, (struct vrend_renderer_resource_info *)info);
   if (ret == 0)
       info->gbm_format = virgl_egl_get_gbm_format(info->virgl_format);
   return ret;
}

void virgl_renderer_get_cap_set(uint32_t cap_set, uint32_t *max_ver,
                                uint32_t *max_size)
{
   vrend_renderer_get_cap_set(cap_set, max_ver, max_size);
}

void virgl_renderer_get_rect(int resource_id, struct iovec *iov, unsigned int num_iovs,
                                uint32_t offset, int x, int y, int width, int height)
{
   vrend_renderer_get_rect(resource_id, iov, num_iovs, offset, x, y, width, height);
}


static struct virgl_renderer_callbacks *rcbs;

static void *dev_cookie;
static int use_egl_context;
struct virgl_egl *egl_info;
static struct vrend_if_cbs virgl_cbs;

void vrend_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct iovec *iov, int num_iovs)
{
   vrend_write_to_iovec(iov, num_iovs, offset, data, bytes);
}

void vrend_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert)
{
   int elsize = util_format_get_blocksize(res->format);
   int h;
   uint32_t myoffset = offset;
   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, u_minify(res->width0, level)) * elsize;
//   uint32_t stride = dst_stride ? dst_stride : util_format_get_nblocksx(res->format, box->width) * elsize;

   if (!invert && (stride == util_format_get_nblocksx(res->format, box->width) * elsize))
      vrend_write_to_iovec(iov, num_iovs, offset, myptr, size);
   else if (invert) {
      for (h = box->height - 1; h >= 0; h--) {
         void *sptr = myptr + (h * elsize * box->width);
         vrend_write_to_iovec(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   } else {
      for (h = 0; h < box->height; h++) {
         void *sptr = myptr + (h * elsize * box->width);
         vrend_write_to_iovec(iov, num_iovs, myoffset, sptr, box->width * elsize);
         myoffset += stride;
      }
   }
}

static void virgl_write_fence(uint32_t fence_id)
{
   rcbs->write_fence(dev_cookie, fence_id);   
}

static virgl_renderer_gl_context create_gl_context(int scanout_idx, struct virgl_gl_ctx_param *param)
{
    struct virgl_renderer_gl_ctx_param vparam;
    if (use_egl_context)
        return virgl_egl_create_context(egl_info, param);
    vparam.version = 1;
    vparam.shared = param->shared;
    vparam.major_ver = param->major_ver;
    vparam.minor_ver = param->minor_ver;
    return rcbs->create_gl_context(dev_cookie, scanout_idx, &vparam);
}

static void destroy_gl_context(virgl_renderer_gl_context ctx)
{
    if (use_egl_context)
        return virgl_egl_destroy_context(egl_info, ctx);
    return rcbs->destroy_gl_context(dev_cookie, ctx);
}

static int make_current(int scanout_idx, virgl_renderer_gl_context ctx)
{
    if (use_egl_context)
        return virgl_egl_make_context_current(egl_info, ctx);
    return rcbs->make_current(dev_cookie, scanout_idx, ctx);
}

static struct vrend_if_cbs virgl_cbs = {
   virgl_write_fence,
   create_gl_context,
   destroy_gl_context,
   make_current,
};

void *virgl_renderer_get_cursor_data(uint32_t resource_id, uint32_t *width, uint32_t *height)
{
   return vrend_renderer_get_cursor_contents(resource_id, width, height);
}

void virgl_renderer_poll(void)
{
   virgl_renderer_force_ctx_0();
   vrend_renderer_check_queries();
   vrend_renderer_check_fences();
}

void virgl_renderer_cleanup(void *cookie)
{
   vrend_renderer_fini();
   if (use_egl_context) {
      virgl_egl_destroy(egl_info);
      egl_info = NULL;
      use_egl_context = 0;
   }
}

int virgl_renderer_init(void *cookie, int flags, struct virgl_renderer_callbacks *cbs)
{
   if (!cookie || !cbs)
     return -1;

   if (cbs->version != 1)
      return -1;

   dev_cookie = cookie;
   rcbs = cbs;

   if (flags & VIRGL_RENDERER_USE_EGL) {
       egl_info = virgl_egl_init();
       if (!egl_info)
           return -1;
       use_egl_context = 1;
   }

   vrend_renderer_init(&virgl_cbs);
   return 0;
}

int virgl_renderer_get_fd_for_texture(uint32_t tex_id, int *fd)
{
    return virgl_egl_get_fd_for_texture(egl_info, tex_id, fd);
}

void virgl_renderer_reset(void)
{
   vrend_renderer_reset();
}
