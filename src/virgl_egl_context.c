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
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdbool.h>
#include <epoxy/egl.h>
#include <gbm.h>
#include <xf86drm.h>
#include "virglrenderer.h"
#include "virgl_egl.h"

#include "virgl_hw.h"
struct virgl_egl {
   int fd;
   struct gbm_device *gbm_dev;
   EGLDisplay egl_display;
   EGLConfig egl_conf;
   EGLContext egl_ctx;
   bool have_mesa_drm_image;
   bool have_mesa_dma_buf_img_export;
   bool have_khr_gl_colorspace;
   bool have_ext_image_dma_buf_import;
   bool have_ext_image_dma_buf_import_modifiers;
};

static int egl_rendernode_open(void)
{
   DIR *dir;
   struct dirent *dir_ent;
   int ret, fd;
   char *rendernode_name;
   dir = opendir("/dev/dri");
   if (!dir)
      return -1;

   fd = -1;
   while ((dir_ent = readdir(dir))) {
      if (dir_ent->d_type != DT_CHR)
         continue;

      if (strncmp(dir_ent->d_name, "renderD", 7))
         continue;

      ret = asprintf(&rendernode_name, "/dev/dri/%s", dir_ent->d_name);
      if (ret < 0)
         return -1;

      fd = open(rendernode_name, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
      if (fd < 0){
         free(rendernode_name);
         continue;
      }

      free(rendernode_name);
      break;
   }

   closedir(dir);
   if (fd < 0)
      return -1;
   return fd;
}

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

struct virgl_egl *virgl_egl_init(int fd, bool surfaceless, bool gles)
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

   /*
    * The surfaceless flag (specified with VIRGL_RENDERER_USE_SURFACELESS)
    * takes precedence and will attempt to get a display of type
    * EGL_PLATFORM_SURFACELESS_MESA.
    * If surfaceless is not specified, an fd supplied with the get_drm_fd
    * is used to open a GBM device.
    * If not provided, /dev/dri rendernodes are scanned and used to open
    * a GBM device.
    * If none of the above results in a valid display, a fallback will be
    * done to use the default display, as long as an fd hasn't been explicitly
    * provided.
    */

   if (surfaceless) {
      conf_att[1] = EGL_PBUFFER_BIT;
      egl->fd = -1;
      egl->gbm_dev = NULL;
   } else {
      if (fd >= 0) {
         egl->fd = fd;
      } else {
         egl->fd = egl_rendernode_open();
      }
      if (egl->fd == -1)
         goto fail;
      egl->gbm_dev = gbm_create_device(egl->fd);
      if (!egl->gbm_dev)
         goto fail;
   }

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
                                                  (EGLNativeDisplayType)egl->gbm_dev, NULL);
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
                                                 (EGLNativeDisplayType)egl->gbm_dev, NULL);
   } else {
      egl->egl_display = eglGetDisplay((EGLNativeDisplayType)egl->gbm_dev);
   }

   if (!egl->egl_display) {
      if (egl->gbm_dev) {
	 gbm_device_destroy(egl->gbm_dev);
	 egl->gbm_dev = NULL;
      }

      if (egl->fd >= 0) {
	 close(egl->fd);
	 egl->fd = -1;
      }

      /* Fallback to using the default display unless an fd was specified. */
      if (fd < 0)
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
   if (egl->gbm_dev) {
      gbm_device_destroy(egl->gbm_dev);
   }

   if (egl->fd >= 0) {
      close(egl->fd);
   }

   free(egl);
   return NULL;
}

void virgl_egl_destroy(struct virgl_egl *egl)
{
   eglMakeCurrent(egl->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                  EGL_NO_CONTEXT);
   eglDestroyContext(egl->egl_display, egl->egl_ctx);
   eglTerminate(egl->egl_display);
   if (egl->gbm_dev)
      gbm_device_destroy(egl->gbm_dev);
   if (egl->fd >= 0)
      close(egl->fd);
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
   *fourcc = virgl_egl_get_gbm_format(format);
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

      vrend_printf("image exported %d %d\n", handle, stride);

      ret = drmPrimeHandleToFD(egl->fd, handle, DRM_CLOEXEC, fd);
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

uint32_t virgl_egl_get_gbm_format(uint32_t format)
{
   switch (format) {
   case VIRGL_FORMAT_B8G8R8X8_UNORM:
   case VIRGL_FORMAT_B8G8R8A8_UNORM:
      return GBM_FORMAT_ARGB8888;
   default:
      vrend_printf( "unknown format to convert to GBM %d\n", format);
      return 0;
   }
}
