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
/* create our own EGL offscreen rendering context via gbm and rendernodes */


/* if we are using EGL and rendernodes then we talk via file descriptors to the remote
   node */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define EGL_EGLEXT_PROTOTYPES
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <epoxy/egl.h>
#include <xf86drm.h>
#include "virglrenderer.h"
#include "virgl_egl.h"
#include "virgl_hw.h"
#include "virgl_gbm.h"

struct virgl_egl {
   struct virgl_gbm *gbm;
   EGLDisplay egl_display;
   EGLConfig egl_conf;
   EGLContext egl_ctx;
   bool have_mesa_drm_image;
   bool have_mesa_dma_buf_img_export;
   bool have_khr_gl_colorspace;
   bool have_ext_image_dma_buf_import;
   bool have_ext_image_dma_buf_import_modifiers;
};

static bool virgl_egl_has_extension_in_string(const char *haystack, const char *needle)
{
   const unsigned needle_len = strlen(needle);

   if (needle_len == 0)
      return false;

   while (true) {
      const char *const s = strstr(haystack, needle);

      if (s == NULL)
         return false;

      if (s[needle_len] == ' ' || s[needle_len] == '\0') {
         return true;
      }

      /* strstr found an extension whose name begins with
       * needle, but whose name is not equal to needle.
       * Restart the search at s + needle_len so that we
       * don't just find the same extension again and go
       * into an infinite loop.
       */
      haystack = s + needle_len;
   }

   return false;
}

struct virgl_egl *virgl_egl_init(struct virgl_gbm *gbm, bool surfaceless, bool gles)
{
   static EGLint conf_att[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 1,
      EGL_GREEN_SIZE, 1,
      EGL_BLUE_SIZE, 1,
      EGL_ALPHA_SIZE, 0,
      EGL_NONE,
   };
   static const EGLint ctx_att[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
   };
   EGLBoolean success;
   EGLenum api;
   EGLint major, minor, num_configs;
   const char *extension_list;
   struct virgl_egl *egl;

   egl = calloc(1, sizeof(struct virgl_egl));
   if (!egl)
      return NULL;

   if (gles)
      conf_att[3] = EGL_OPENGL_ES2_BIT;

   if (surfaceless)
      conf_att[1] = EGL_PBUFFER_BIT;
   else if (!gbm)
      goto fail;

   egl->gbm = gbm;
   const char *client_extensions = eglQueryString (NULL, EGL_EXTENSIONS);

   if (strstr (client_extensions, "EGL_KHR_platform_base")) {
      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
         (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplay");

      if (!get_platform_display)
        goto fail;

      if (surfaceless) {
         egl->egl_display = get_platform_display (EGL_PLATFORM_SURFACELESS_MESA,
                                                  EGL_DEFAULT_DISPLAY, NULL);
      } else
         egl->egl_display = get_platform_display (EGL_PLATFORM_GBM_KHR,
                                                  (EGLNativeDisplayType)egl->gbm->device, NULL);
   } else if (strstr (client_extensions, "EGL_EXT_platform_base")) {
      PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
         (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplayEXT");

      if (!get_platform_display)
        goto fail;

      if (surfaceless) {
         egl->egl_display = get_platform_display (EGL_PLATFORM_SURFACELESS_MESA,
                                                  EGL_DEFAULT_DISPLAY, NULL);
      } else
         egl->egl_display = get_platform_display (EGL_PLATFORM_GBM_KHR,
                                                 (EGLNativeDisplayType)egl->gbm->device, NULL);
   } else {
      egl->egl_display = eglGetDisplay((EGLNativeDisplayType)egl->gbm->device);
   }

   if (!egl->egl_display) {
      /*
       * Don't fallback to the default display if the fd provided by (*get_drm_fd)
       * can't be used.
       */
      if (egl->gbm && egl->gbm->fd < 0)
         goto fail;

      egl->egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
      if (!egl->egl_display)
         goto fail;
   }

   success = eglInitialize(egl->egl_display, &major, &minor);
   if (!success)
      goto fail;

   extension_list = eglQueryString(egl->egl_display, EGL_EXTENSIONS);
#ifdef VIRGL_EGL_DEBUG
   vrend_printf( "EGL major/minor: %d.%d\n", major, minor);
   vrend_printf( "EGL version: %s\n",
           eglQueryString(egl->egl_display, EGL_VERSION));
   vrend_printf( "EGL vendor: %s\n",
           eglQueryString(egl->egl_display, EGL_VENDOR));
   vrend_printf( "EGL extensions: %s\n", extension_list);
#endif
   /* require surfaceless context */
   if (!virgl_egl_has_extension_in_string(extension_list, "EGL_KHR_surfaceless_context")) {
      vrend_printf( "failed to find support for surfaceless context\n");
      goto fail;
   }

   if (!virgl_egl_has_extension_in_string(extension_list, "EGL_KHR_create_context")) {
      vrend_printf( "failed to find EGL_KHR_create_context extensions\n");
      goto fail;
   }

   egl->have_mesa_drm_image = false;
   egl->have_mesa_dma_buf_img_export = false;
   if (virgl_egl_has_extension_in_string(extension_list, "EGL_MESA_drm_image"))
      egl->have_mesa_drm_image = true;

   if (virgl_egl_has_extension_in_string(extension_list, "EGL_MESA_image_dma_buf_export"))
      egl->have_mesa_dma_buf_img_export = true;

   if (virgl_egl_has_extension_in_string(extension_list, "EGL_EXT_image_dma_buf_import"))
      egl->have_ext_image_dma_buf_import = true;

   if (virgl_egl_has_extension_in_string(extension_list, "EGL_EXT_image_dma_buf_import_modifiers"))
      egl->have_ext_image_dma_buf_import_modifiers = true;

   egl->have_khr_gl_colorspace =
         virgl_egl_has_extension_in_string(extension_list, "EGL_KHR_gl_colorspace");

   if (gles)
      api = EGL_OPENGL_ES_API;
   else
      api = EGL_OPENGL_API;
   success = eglBindAPI(api);
   if (!success)
      goto fail;

   success = eglChooseConfig(egl->egl_display, conf_att, &egl->egl_conf,
                             1, &num_configs);
   if (!success || num_configs != 1)
      goto fail;

   egl->egl_ctx = eglCreateContext(egl->egl_display, egl->egl_conf, EGL_NO_CONTEXT,
                                   ctx_att);
   if (!egl->egl_ctx)
      goto fail;

   eglMakeCurrent(egl->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                  egl->egl_ctx);
   return egl;

 fail:
   free(egl);
   return NULL;
}

void virgl_egl_destroy(struct virgl_egl *egl)
{
   eglMakeCurrent(egl->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                  EGL_NO_CONTEXT);
   eglDestroyContext(egl->egl_display, egl->egl_ctx);
   eglTerminate(egl->egl_display);
   free(egl);
}

virgl_renderer_gl_context virgl_egl_create_context(struct virgl_egl *egl, struct virgl_gl_ctx_param *vparams)
{
   EGLContext egl_ctx;
   EGLint ctx_att[] = {
      EGL_CONTEXT_CLIENT_VERSION, vparams->major_ver,
      EGL_CONTEXT_MINOR_VERSION_KHR, vparams->minor_ver,
      EGL_NONE
   };
   egl_ctx = eglCreateContext(egl->egl_display,
                             egl->egl_conf,
                             vparams->shared ? eglGetCurrentContext() : EGL_NO_CONTEXT,
                             ctx_att);
   return (virgl_renderer_gl_context)egl_ctx;
}

void virgl_egl_destroy_context(struct virgl_egl *egl, virgl_renderer_gl_context virglctx)
{
   EGLContext egl_ctx = (EGLContext)virglctx;
   eglDestroyContext(egl->egl_display, egl_ctx);
}

int virgl_egl_make_context_current(struct virgl_egl *egl, virgl_renderer_gl_context virglctx)
{
   EGLContext egl_ctx = (EGLContext)virglctx;

   return eglMakeCurrent(egl->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         egl_ctx);
}

virgl_renderer_gl_context virgl_egl_get_current_context(UNUSED struct virgl_egl *egl)
{
   EGLContext egl_ctx = eglGetCurrentContext();
   return (virgl_renderer_gl_context)egl_ctx;
}

int virgl_egl_get_fourcc_for_texture(struct virgl_egl *egl, uint32_t tex_id, uint32_t format, int *fourcc)
{
   int ret = EINVAL;
   uint32_t gbm_format = 0;

   EGLImageKHR image;
   EGLBoolean success;

   if (!egl->have_mesa_dma_buf_img_export) {
      ret = 0;
      goto fallback;
   }

   image = eglCreateImageKHR(egl->egl_display, eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
                            (EGLClientBuffer)(unsigned long)tex_id, NULL);

   if (!image)
      return EINVAL;

   success = eglExportDMABUFImageQueryMESA(egl->egl_display, image, fourcc, NULL, NULL);
   if (!success)
      goto out_destroy;
   ret = 0;
 out_destroy:
   eglDestroyImageKHR(egl->egl_display, image);
   return ret;

 fallback:
   ret = virgl_gbm_convert_format(&format, &gbm_format);
   *fourcc = (int)gbm_format;
   return ret;
}

int virgl_egl_get_fd_for_texture2(struct virgl_egl *egl, uint32_t tex_id, int *fd,
                                  int *stride, int *offset)
{
   int ret = EINVAL;
   EGLImageKHR image = eglCreateImageKHR(egl->egl_display, eglGetCurrentContext(),
                                         EGL_GL_TEXTURE_2D_KHR,
                                         (EGLClientBuffer)(unsigned long)tex_id, NULL);
   if (!image)
      return EINVAL;
   if (!egl->have_mesa_dma_buf_img_export)
      goto out_destroy;

   if (!eglExportDMABUFImageMESA(egl->egl_display, image, fd,
                                 stride, offset))
      goto out_destroy;

   ret = 0;

out_destroy:
   eglDestroyImageKHR(egl->egl_display, image);
   return ret;
}

int virgl_egl_get_fd_for_texture(struct virgl_egl *egl, uint32_t tex_id, int *fd)
{
   EGLImageKHR image;
   EGLint stride;
   EGLint offset;
   EGLBoolean success;
   int ret;
   image = eglCreateImageKHR(egl->egl_display, eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
                            (EGLClientBuffer)(unsigned long)tex_id, NULL);

   if (!image)
      return EINVAL;

   ret = EINVAL;
   if (egl->have_mesa_dma_buf_img_export) {
      success = eglExportDMABUFImageMESA(egl->egl_display, image, fd, &stride,
                                         &offset);
      if (!success)
         goto out_destroy;
   } else if (egl->have_mesa_drm_image) {
      EGLint handle;
      success = eglExportDRMImageMESA(egl->egl_display, image, NULL, &handle,
                                      &stride);

      if (!success)
         goto out_destroy;

      if (!egl->gbm)
         goto out_destroy;

      ret = virgl_gbm_export_fd(egl->gbm->device, handle, fd);
      if (ret < 0)
         goto out_destroy;
   } else {
      goto out_destroy;
   }

   ret = 0;
 out_destroy:
   eglDestroyImageKHR(egl->egl_display, image);
   return ret;
}

bool virgl_has_egl_khr_gl_colorspace(struct virgl_egl *egl)
{
   return egl->have_khr_gl_colorspace;
}

void *virgl_egl_image_from_dmabuf(struct virgl_egl *egl, struct gbm_bo *bo)
{
   int ret;
   EGLImageKHR image;
   int fds[4] = {-1, -1, -1, -1};
   int num_planes = gbm_bo_get_plane_count(bo);
   // When the bo has 3 planes with modifier support, it requires 37 components.
   EGLint khr_image_attrs[37] = {
      EGL_WIDTH,
      gbm_bo_get_width(bo),
      EGL_HEIGHT,
      gbm_bo_get_height(bo),
      EGL_LINUX_DRM_FOURCC_EXT,
      (int)gbm_bo_get_format(bo),
      EGL_NONE,
   };

   if (num_planes < 0 || num_planes > 4)
      return (void *)EGL_NO_IMAGE_KHR;

   for (int plane = 0; plane < num_planes; plane++) {
      uint32_t handle = gbm_bo_get_handle_for_plane(bo, plane).u32;
      ret = virgl_gbm_export_fd(egl->gbm->device, handle, &fds[plane]);
      if (ret < 0) {
         vrend_printf( "failed to export plane handle\n");
         image = (void *)EGL_NO_IMAGE_KHR;
         goto out_close;
      }
   }

   size_t attrs_index = 6;
   for (int plane = 0; plane < num_planes; plane++) {
      khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_FD_EXT + plane * 3;
      khr_image_attrs[attrs_index++] = fds[plane];
      khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT + plane * 3;
      khr_image_attrs[attrs_index++] = gbm_bo_get_offset(bo, plane);
      khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT + plane * 3;
      khr_image_attrs[attrs_index++] = gbm_bo_get_stride_for_plane(bo, plane);
      if (egl->have_ext_image_dma_buf_import_modifiers) {
         const uint64_t modifier = gbm_bo_get_modifier(bo);
         khr_image_attrs[attrs_index++] =
         EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT + plane * 2;
         khr_image_attrs[attrs_index++] = modifier & 0xfffffffful;
         khr_image_attrs[attrs_index++] =
         EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT + plane * 2;
         khr_image_attrs[attrs_index++] = modifier >> 32;
      }
   }

   khr_image_attrs[attrs_index++] = EGL_NONE;
   image = eglCreateImageKHR(egl->egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL,
                             khr_image_attrs);

out_close:
   for (int plane = 0; plane < num_planes; plane++)
      close(fds[plane]);

   return (void*)image;
}

void virgl_egl_image_destroy(struct virgl_egl *egl, void *image)
{
   eglDestroyImageKHR(egl->egl_display, image);
}
