/**************************************************************************
 *
 * Copyright (C) 2015 Red Hat Inc.
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>

#include "virgl_hw.h"
#include "virglrenderer.h"

#include <sys/uio.h>
#include "vtest.h"
#include "vtest_protocol.h"
#include "util.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_hash_table.h"


static int ctx_id = 1;
static int fence_id = 1;

static int last_fence;
static void vtest_write_fence(UNUSED void *cookie, uint32_t fence_id_in)
{
   last_fence = fence_id_in;
}

struct virgl_renderer_callbacks vtest_cbs = {
   .version = 1,
   .write_fence = vtest_write_fence,
};

struct vtest_renderer {
   int in_fd;
   int out_fd;
   unsigned protocol_version;
   struct util_hash_table *iovec_hash;
};

struct vtest_renderer renderer;

static int
__failed_call(const char* func, const char *called, int ret)
{
   fprintf(stderr, "%s called %s which failed (%d)\n", func, called, ret);
   return ret;
}

#define report_failed_call(called, ret) \
   __failed_call(__FUNCTION__, called, ret)

static int
__failure(const char* func, const char *reason, int ret)
{
   fprintf(stderr, "%s %s (%d)\n", func, reason, ret);
   return ret;
}

#define report_failure(reason, ret) \
   __failure(__FUNCTION__, reason, ret)

static unsigned
hash_func(void *key)
{
   intptr_t ip = pointer_to_intptr(key);
   return (unsigned)(ip & 0xffffffff);
}

static int
compare_iovecs(void *key1, void *key2)
{
   if (key1 < key2) {
      return -1;
   } else if (key1 > key2) {
      return 1;
   } else {
      return 0;
   }
}

static void free_iovec(void *value)
{
   struct iovec *iovec = value;

   free(iovec->iov_base);
   free(iovec);
}

static int vtest_block_write(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   left = size;

   do {
      ret = write(fd, ptr, left);
      if (ret < 0) {
         return -errno;
      }

      left -= ret;
      ptr += ret;
   } while (left);

   return size;
}

static int vtest_block_write_zero(int fd, int size)
{
   char zero[256] = {0};
   int left;
   int ret;
   left = size;

   do {
      ret = write(fd, zero, MIN2(left, 256));
      if (ret < 0) {
         return -errno;
      }

      left -= ret;
   } while (left);

   return size;
}

int vtest_block_read(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   static int savefd = -1;

   left = size;
   do {
      ret = read(fd, ptr, left);
      if (ret <= 0) {
         return ret == -1 ? -errno : 0;
      }

      left -= ret;
      ptr += ret;
   } while (left);

   if (getenv("VTEST_SAVE")) {
      if (savefd == -1) {
         savefd = open(getenv("VTEST_SAVE"),
                       O_CLOEXEC|O_CREAT|O_WRONLY|O_TRUNC|O_DSYNC, S_IRUSR|S_IWUSR);
         if (savefd == -1) {
            perror("error opening save file");
            exit(1);
         }
      }
      if (write(savefd, buf, size) != size) {
         perror("failed to save");
         exit(1);
      }
   }

   return size;
}

int vtest_create_renderer(int in_fd, int out_fd, uint32_t length,
                          int ctx_flags)
{
   char *vtestname;
   int ret;

   renderer.iovec_hash = util_hash_table_create(hash_func, compare_iovecs, free_iovec);
   renderer.in_fd = in_fd;
   renderer.out_fd = out_fd;

   /* By default we support version 0 unless VCMD_PROTOCOL_VERSION is sent */
   renderer.protocol_version = 0;

   ret = virgl_renderer_init(&renderer,
         ctx_flags | VIRGL_RENDERER_THREAD_SYNC, &vtest_cbs);
   if (ret) {
      fprintf(stderr, "failed to initialise renderer.\n");
      return -1;
   }

   vtestname = calloc(1, length + 1);
   if (!vtestname) {
      return -1;
   }

   ret = vtest_block_read(renderer.in_fd, vtestname, length);
   if (ret != (int)length) {
      ret = -1;
      goto end;
   }

   ret = virgl_renderer_context_create(ctx_id, strlen(vtestname), vtestname);

end:
   free(vtestname);
   return ret;
}

int vtest_ping_protocol_version(UNUSED uint32_t length_dw)
{
   uint32_t hdr_buf[VTEST_HDR_SIZE];
   int ret;

   hdr_buf[VTEST_CMD_LEN] = VCMD_PING_PROTOCOL_VERSION_SIZE;
   hdr_buf[VTEST_CMD_ID] = VCMD_PING_PROTOCOL_VERSION;
   ret = vtest_block_write(renderer.out_fd, hdr_buf, sizeof(hdr_buf));
   if (ret < 0) {
      return ret;
   }

   return 0;
}

int vtest_protocol_version(UNUSED uint32_t length_dw)
{
   uint32_t hdr_buf[VTEST_HDR_SIZE];
   uint32_t version_buf[VCMD_PROTOCOL_VERSION_SIZE];
   int ret;

   ret = vtest_block_read(renderer.in_fd, &version_buf, sizeof(version_buf));
   if (ret != sizeof(version_buf))
      return -1;

   renderer.protocol_version = MIN2(version_buf[VCMD_PROTOCOL_VERSION_VERSION],
                                    VTEST_PROTOCOL_VERSION);

   hdr_buf[VTEST_CMD_LEN] = VCMD_PROTOCOL_VERSION_SIZE;
   hdr_buf[VTEST_CMD_ID] = VCMD_PROTOCOL_VERSION;

   version_buf[VCMD_PROTOCOL_VERSION_VERSION] = renderer.protocol_version;

   ret = vtest_block_write(renderer.out_fd, hdr_buf, sizeof(hdr_buf));
   if (ret < 0) {
      return ret;
   }

   ret = vtest_block_write(renderer.out_fd, version_buf, sizeof(version_buf));
   if (ret < 0) {
      return ret;
   }

   return 0;
}

void vtest_destroy_renderer(void)
{
   virgl_renderer_context_destroy(ctx_id);
   virgl_renderer_cleanup(&renderer);
   renderer.in_fd = -1;
   renderer.out_fd = -1;
}

int vtest_send_caps2(UNUSED uint32_t length_dw)
{
   uint32_t hdr_buf[2];
   void *caps_buf;
   int ret;
   uint32_t max_ver, max_size;

   virgl_renderer_get_cap_set(2, &max_ver, &max_size);

   if (max_size == 0) {
      return -1;
   }

   caps_buf = malloc(max_size);
   if (!caps_buf) {
      return -1;
   }

   virgl_renderer_fill_caps(2, 1, caps_buf);

   hdr_buf[0] = max_size + 1;
   hdr_buf[1] = 2;
   ret = vtest_block_write(renderer.out_fd, hdr_buf, 8);
   if (ret < 0) {
      goto end;
   }

   vtest_block_write(renderer.out_fd, caps_buf, max_size);
   if (ret < 0) {
      goto end;
   }

end:
   free(caps_buf);
   return 0;
}

int vtest_send_caps(UNUSED uint32_t length_dw)
{
   uint32_t  max_ver, max_size;
   void *caps_buf;
   uint32_t hdr_buf[2];
   int ret;

   virgl_renderer_get_cap_set(1, &max_ver, &max_size);

   caps_buf = malloc(max_size);
   if (!caps_buf) {
      return -1;
   }

   virgl_renderer_fill_caps(1, 1, caps_buf);

   hdr_buf[0] = max_size + 1;
   hdr_buf[1] = 1;
   ret = vtest_block_write(renderer.out_fd, hdr_buf, 8);
   if (ret < 0) {
      goto end;
   }

   vtest_block_write(renderer.out_fd, caps_buf, max_size);
   if (ret < 0) {
      goto end;
   }

end:
   free(caps_buf);
   return 0;
}

int vtest_create_resource(UNUSED uint32_t length_dw)
{
   uint32_t res_create_buf[VCMD_RES_CREATE_SIZE];
   struct virgl_renderer_resource_create_args args;
   int ret;

   ret = vtest_block_read(renderer.in_fd, &res_create_buf, sizeof(res_create_buf));
   if (ret != sizeof(res_create_buf)) {
      return -1;
   }

   args.handle = res_create_buf[VCMD_RES_CREATE_RES_HANDLE];
   args.target = res_create_buf[VCMD_RES_CREATE_TARGET];
   args.format = res_create_buf[VCMD_RES_CREATE_FORMAT];
   args.bind = res_create_buf[VCMD_RES_CREATE_BIND];

   args.width = res_create_buf[VCMD_RES_CREATE_WIDTH];
   args.height = res_create_buf[VCMD_RES_CREATE_HEIGHT];
   args.depth = res_create_buf[VCMD_RES_CREATE_DEPTH];
   args.array_size = res_create_buf[VCMD_RES_CREATE_ARRAY_SIZE];
   args.last_level = res_create_buf[VCMD_RES_CREATE_LAST_LEVEL];
   args.nr_samples = res_create_buf[VCMD_RES_CREATE_NR_SAMPLES];
   args.flags = 0;

   ret = virgl_renderer_resource_create(&args, NULL, 0);

   virgl_renderer_ctx_attach_resource(ctx_id, args.handle);
   return ret;
}

int vtest_create_resource2(UNUSED uint32_t length_dw)
{
   uint32_t res_create_buf[VCMD_RES_CREATE2_SIZE];
   struct virgl_renderer_resource_create_args args;
   struct iovec *iovec;
   int ret;

   ret = vtest_block_read(renderer.in_fd, &res_create_buf, sizeof(res_create_buf));
   if (ret != sizeof(res_create_buf)) {
      return -1;
   }

   args.handle = res_create_buf[VCMD_RES_CREATE2_RES_HANDLE];
   args.target = res_create_buf[VCMD_RES_CREATE2_TARGET];
   args.format = res_create_buf[VCMD_RES_CREATE2_FORMAT];
   args.bind = res_create_buf[VCMD_RES_CREATE2_BIND];

   args.width = res_create_buf[VCMD_RES_CREATE2_WIDTH];
   args.height = res_create_buf[VCMD_RES_CREATE2_HEIGHT];
   args.depth = res_create_buf[VCMD_RES_CREATE2_DEPTH];
   args.array_size = res_create_buf[VCMD_RES_CREATE2_ARRAY_SIZE];
   args.last_level = res_create_buf[VCMD_RES_CREATE2_LAST_LEVEL];
   args.nr_samples = res_create_buf[VCMD_RES_CREATE2_NR_SAMPLES];
   args.flags = 0;

   // Check that the handle doesn't already exist.
   if (util_hash_table_get(renderer.iovec_hash, intptr_to_pointer(args.handle))) {
      return -EEXIST;
   }

   ret = virgl_renderer_resource_create(&args, NULL, 0);
   if (ret) {
      return ret;
   }

   virgl_renderer_ctx_attach_resource(ctx_id, args.handle);

   iovec = CALLOC_STRUCT(iovec);
   if (!iovec) {
      return -ENOMEM;
   }

   iovec->iov_len = res_create_buf[VCMD_RES_CREATE2_DATA_SIZE];
   iovec->iov_base = calloc(1, iovec->iov_len);
   if (!iovec->iov_base) {
      FREE(iovec);
      return -ENOMEM;
   }

   virgl_renderer_resource_attach_iov(args.handle, iovec, 1);
   util_hash_table_set(renderer.iovec_hash, intptr_to_pointer(args.handle), iovec);

   return ret;
}

int vtest_resource_unref(UNUSED uint32_t length_dw)
{
   uint32_t res_unref_buf[VCMD_RES_UNREF_SIZE];
   int ret;
   uint32_t handle;

   ret = vtest_block_read(renderer.in_fd, &res_unref_buf, sizeof(res_unref_buf));
   if (ret != sizeof(res_unref_buf)) {
      return -1;
   }

   handle = res_unref_buf[VCMD_RES_UNREF_RES_HANDLE];
   virgl_renderer_ctx_attach_resource(ctx_id, handle);

   virgl_renderer_resource_detach_iov(handle, NULL, NULL);
   util_hash_table_remove(renderer.iovec_hash, intptr_to_pointer(handle));

   virgl_renderer_resource_unref(handle);
   return 0;
}

int vtest_submit_cmd(uint32_t length_dw)
{
   uint32_t *cbuf;
   int ret;

   if (length_dw > UINT_MAX / 4) {
      return -1;
   }

   cbuf = malloc(length_dw * 4);
   if (!cbuf) {
      return -1;
   }

   ret = vtest_block_read(renderer.in_fd, cbuf, length_dw * 4);
   if (ret != (int)length_dw * 4) {
      free(cbuf);
      return -1;
   }

   virgl_renderer_submit_cmd(cbuf, ctx_id, length_dw);

   free(cbuf);
   return 0;
}

#define DECODE_TRANSFER \
   do {								\
      handle = thdr_buf[VCMD_TRANSFER_RES_HANDLE];		\
      level = thdr_buf[VCMD_TRANSFER_LEVEL];			\
      stride = thdr_buf[VCMD_TRANSFER_STRIDE];			\
      layer_stride = thdr_buf[VCMD_TRANSFER_LAYER_STRIDE];	\
      box.x = thdr_buf[VCMD_TRANSFER_X];			\
      box.y = thdr_buf[VCMD_TRANSFER_Y];			\
      box.z = thdr_buf[VCMD_TRANSFER_Z];			\
      box.w = thdr_buf[VCMD_TRANSFER_WIDTH];			\
      box.h = thdr_buf[VCMD_TRANSFER_HEIGHT];			\
      box.d = thdr_buf[VCMD_TRANSFER_DEPTH];			\
      data_size = thdr_buf[VCMD_TRANSFER_DATA_SIZE];		\
   } while(0)


int vtest_transfer_get(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
   int ret;
   int level;
   uint32_t stride, layer_stride, handle;
   struct virgl_box box;
   uint32_t data_size;
   void *ptr;
   struct iovec iovec;

   ret = vtest_block_read(renderer.in_fd, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
   if (ret != VCMD_TRANSFER_HDR_SIZE * 4) {
      return ret;
   }

   DECODE_TRANSFER;

   ptr = malloc(data_size);
   if (!ptr) {
      return -ENOMEM;
   }

   iovec.iov_len = data_size;
   iovec.iov_base = ptr;
   ret = virgl_renderer_transfer_read_iov(handle,
         ctx_id,
         level,
         stride,
         layer_stride,
         &box,
         0,
         &iovec, 1);
   if (ret) {
      fprintf(stderr," transfer read failed %d\n", ret);
   }

   ret = vtest_block_write(renderer.out_fd, ptr, data_size);

   free(ptr);
   return ret < 0 ? ret : 0;
}

int vtest_transfer_put(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
   int ret;
   int level;
   uint32_t stride, layer_stride, handle;
   struct virgl_box box;
   uint32_t data_size;
   void *ptr;
   struct iovec iovec;

   ret = vtest_block_read(renderer.in_fd, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
   if (ret != VCMD_TRANSFER_HDR_SIZE * 4) {
      return ret;
   }

   DECODE_TRANSFER;

   ptr = malloc(data_size);
   if (!ptr) {
      return -ENOMEM;
   }

   ret = vtest_block_read(renderer.in_fd, ptr, data_size);
   if (ret < 0) {
      return ret;
   }

   iovec.iov_len = data_size;
   iovec.iov_base = ptr;
   ret = virgl_renderer_transfer_write_iov(handle,
                                           ctx_id,
                                           level,
                                           stride,
                                           layer_stride,
                                           &box,
                                           0,
                                           &iovec, 1);
   if (ret) {
      fprintf(stderr," transfer write failed %d\n", ret);
   }

   free(ptr);
   return 0;
}

#define DECODE_TRANSFER2 \
   do {							\
      handle = thdr_buf[VCMD_TRANSFER2_RES_HANDLE];	\
      level = thdr_buf[VCMD_TRANSFER2_LEVEL];		\
      box.x = thdr_buf[VCMD_TRANSFER2_X];		\
      box.y = thdr_buf[VCMD_TRANSFER2_Y];		\
      box.z = thdr_buf[VCMD_TRANSFER2_Z];		\
      box.w = thdr_buf[VCMD_TRANSFER2_WIDTH];		\
      box.h = thdr_buf[VCMD_TRANSFER2_HEIGHT];		\
      box.d = thdr_buf[VCMD_TRANSFER2_DEPTH];		\
      data_size = thdr_buf[VCMD_TRANSFER2_DATA_SIZE];	\
      offset = thdr_buf[VCMD_TRANSFER2_OFFSET];		\
   } while(0)


int vtest_transfer_get2(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER2_HDR_SIZE];
   int ret;
   int level;
   uint32_t handle;
   struct virgl_box box;
   uint32_t data_size;
   uint32_t offset;
   uint32_t extra_data = 0;
   struct iovec *iovec;

   ret = vtest_block_read(renderer.in_fd, thdr_buf, sizeof(thdr_buf));
   if (ret != sizeof(thdr_buf)) {
      return ret;
   }

   DECODE_TRANSFER2;

   iovec = util_hash_table_get(renderer.iovec_hash, intptr_to_pointer(handle));
   if (!iovec) {
      return report_failed_call("util_hash_table_get", -ESRCH);
   }

   if (offset >= iovec->iov_len) {
      return report_failure("offset larger then length of backing store", -EFAULT);
   }

   ret = virgl_renderer_transfer_read_iov(handle,
                                          ctx_id,
                                          level,
                                          0,
                                          0,
                                          &box,
                                          offset,
                                          NULL, 0);
   if (ret) {
      return report_failed_call("virgl_renderer_transfer_read_iov", ret);
   }

   /* Make sure we don't read out of bounds. */
   if (data_size > (iovec->iov_len - offset)) {
      extra_data = data_size - (iovec->iov_len - offset);
      data_size -= extra_data;
   }

   ret = vtest_block_write(renderer.out_fd,
                           iovec->iov_base + offset,
                           data_size);
   if (ret < 0) {
      return report_failed_call("vtest_block_write", ret);
   }

   if (extra_data) {
      ret = vtest_block_write_zero(renderer.out_fd, extra_data);
      if (ret < 0) {
         return report_failed_call("vtest_block_write_zero", ret);
      }
   }

   return ret < 0 ? ret : 0;
}

int vtest_transfer_put2(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER2_HDR_SIZE];
   int ret;
   int level;
   uint32_t handle;
   struct virgl_box box;
   uint32_t data_size;
   uint32_t offset;
   struct iovec *iovec;

   ret = vtest_block_read(renderer.in_fd, thdr_buf, sizeof(thdr_buf));
   if (ret != sizeof(thdr_buf)) {
      return ret;
   }

   DECODE_TRANSFER2;

   iovec = util_hash_table_get(renderer.iovec_hash, intptr_to_pointer(handle));
   if (!iovec) {
      return report_failed_call("util_hash_table_get", -ESRCH);
   }

   ret = vtest_block_read(renderer.in_fd, iovec->iov_base + offset, data_size);
   if (ret < 0) {
      return report_failed_call("vtest_block_read", ret);
   }

   ret = virgl_renderer_transfer_write_iov(handle,
                                           ctx_id,
                                           level,
                                           0,
                                           0,
                                           &box,
                                           offset,
                                           NULL, 0);
   if (ret) {
      return report_failed_call("virgl_renderer_transfer_write_iov", ret);
   }

   return 0;
}

int vtest_resource_busy_wait(UNUSED uint32_t length_dw)
{
   uint32_t bw_buf[VCMD_BUSY_WAIT_SIZE];
   int ret, fd;
   int flags;
   uint32_t hdr_buf[VTEST_HDR_SIZE];
   uint32_t reply_buf[1];
   bool busy = false;

   ret = vtest_block_read(renderer.in_fd, &bw_buf, sizeof(bw_buf));
   if (ret != sizeof(bw_buf)) {
      return -1;
   }

   /*  handle = bw_buf[VCMD_BUSY_WAIT_HANDLE]; unused as of now */
   flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];

   if (flags == VCMD_BUSY_WAIT_FLAG_WAIT) {
      do {
         if (last_fence == (fence_id - 1)) {
            break;
         }

         fd = virgl_renderer_get_poll_fd();
         if (fd != -1) {
            vtest_wait_for_fd_read(fd);
         }

         virgl_renderer_poll();
      } while (1);

      busy = false;
   } else {
      busy = last_fence != (fence_id - 1);
   }

   hdr_buf[VTEST_CMD_LEN] = 1;
   hdr_buf[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;
   reply_buf[0] = busy ? 1 : 0;

   ret = vtest_block_write(renderer.out_fd, hdr_buf, sizeof(hdr_buf));
   if (ret < 0) {
      return ret;
   }

   ret = vtest_block_write(renderer.out_fd, reply_buf, sizeof(reply_buf));
   if (ret < 0) {
      return ret;
   }

   return 0;
}

int vtest_renderer_create_fence(void)
{
   virgl_renderer_create_fence(fence_id++, ctx_id);
   return 0;
}

int vtest_poll(void)
{
   virgl_renderer_poll();
   return 0;
}
