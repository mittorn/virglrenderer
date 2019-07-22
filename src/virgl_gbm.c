/**************************************************************************
 *
 * Copyright (C) 2019 Chromium.
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <xf86drm.h>
#include <unistd.h>

#include "virgl_gbm.h"
#include "virgl_hw.h"
#include "vrend_debug.h"

struct planar_layout {
    size_t num_planes;
    int horizontal_subsampling[4];
    int vertical_subsampling[4];
    int bytes_per_pixel[4];
};

static const struct planar_layout packed_1bpp_layout = {
    .num_planes = 1,
    .horizontal_subsampling = { 1 },
    .vertical_subsampling = { 1 },
    .bytes_per_pixel = { 1 }
};

static const struct planar_layout packed_2bpp_layout = {
    .num_planes = 1,
    .horizontal_subsampling = { 1 },
    .vertical_subsampling = { 1 },
    .bytes_per_pixel = { 2 }
};

static const struct planar_layout packed_4bpp_layout = {
    .num_planes = 1,
    .horizontal_subsampling = { 1 },
    .vertical_subsampling = { 1 },
    .bytes_per_pixel = { 4 }
};

static const struct planar_layout biplanar_yuv_420_layout = {
    .num_planes = 2,
    .horizontal_subsampling = { 1, 2 },
    .vertical_subsampling = { 1, 2 },
    .bytes_per_pixel = { 1, 2 }
};

static const struct planar_layout triplanar_yuv_420_layout = {
    .num_planes = 3,
    .horizontal_subsampling = { 1, 2, 2 },
    .vertical_subsampling = { 1, 2, 2 },
    .bytes_per_pixel = { 1, 1, 1 }
};

static int rendernode_open(void)
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

static const struct planar_layout *layout_from_format(uint32_t format)
{
   switch (format) {
   case GBM_FORMAT_R8:
      return &packed_1bpp_layout;
   case GBM_FORMAT_YVU420:
      return &triplanar_yuv_420_layout;
   case GBM_FORMAT_NV12:
      return &biplanar_yuv_420_layout;
   case GBM_FORMAT_RGB565:
      return &packed_2bpp_layout;
   case GBM_FORMAT_ARGB8888:
   case GBM_FORMAT_XRGB8888:
   case GBM_FORMAT_ABGR8888:
   case GBM_FORMAT_XBGR8888:
      return &packed_4bpp_layout;
   default:
      return NULL;
   }
}

struct virgl_gbm *virgl_gbm_init(int fd)
{
   struct virgl_gbm *gbm = calloc(1, sizeof(struct virgl_gbm));
   if (!gbm)
      return NULL;

   gbm->fd = -1;
   if (fd < 0) {
      gbm->fd = rendernode_open();
      if (gbm->fd < 0)
         goto out_error;

      gbm->device = gbm_create_device(gbm->fd);
      if (!gbm->device) {
         close(gbm->fd);
         goto out_error;
      }
   } else {
      gbm->device = gbm_create_device(fd);
      if (!gbm->device)
         goto out_error;
   }

   return gbm;

out_error:
   free(gbm);
   return NULL;
}

void virgl_gbm_fini(struct virgl_gbm *gbm)
{
   gbm_device_destroy(gbm->device);
   if (gbm->fd >= 0)
      close(gbm->fd);
   free(gbm);
}

uint32_t virgl_gbm_convert_format(uint32_t virgl_format)
{
   switch (virgl_format) {
   case VIRGL_FORMAT_B5G6R5_UNORM:
      return GBM_FORMAT_RGB565;
   case VIRGL_FORMAT_B8G8R8A8_UNORM:
      return GBM_FORMAT_ARGB8888;
   case VIRGL_FORMAT_B8G8R8X8_UNORM:
      return GBM_FORMAT_XRGB8888;
   case VIRGL_FORMAT_NV12:
      return GBM_FORMAT_NV12;
   case VIRGL_FORMAT_R8G8B8A8_UNORM:
      return GBM_FORMAT_ABGR8888;
   case VIRGL_FORMAT_R8G8B8X8_UNORM:
      return GBM_FORMAT_XBGR8888;
   case VIRGL_FORMAT_R8_UNORM:
      return GBM_FORMAT_R8;
   case VIRGL_FORMAT_YV12:
      return GBM_FORMAT_YVU420;
   default:
      return 0;
   }
}
