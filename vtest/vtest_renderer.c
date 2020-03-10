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
#include <sys/socket.h>
#include <sys/mman.h>

#include "vtest.h"
#include "vtest_shm.h"
#include "vtest_protocol.h"

#include "util.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/u_memory.h"
#include "util/u_hash_table.h"


static int ctx_id = 1;


struct vtest_renderer {
   struct vtest_input *input;
   int out_fd;
   unsigned protocol_version;
   struct util_hash_table *iovec_hash;
   const char *rendernode_name;

   uint32_t max_length;

   int fence_id;
   int last_fence;
};

static void vtest_write_fence(UNUSED void *cookie, uint32_t fence_id_in)
{
   struct vtest_renderer *renderer = (struct vtest_renderer*)cookie;
   renderer->last_fence = fence_id_in;
}

static int vtest_get_drm_fd(void *cookie)
{
   int fd = -1;
   struct vtest_renderer *renderer = (struct vtest_renderer*)cookie;
   if (!renderer->rendernode_name)
      return -1;
   fd = open(renderer->rendernode_name, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
   if (fd == -1)
      fprintf(stderr, "Unable to open rendernode '%s' falling back to default search\n",
              renderer->rendernode_name);
   return fd;
}

struct virgl_renderer_callbacks vtest_cbs = {
   .version = 2,
   .write_fence = vtest_write_fence,
   .get_drm_fd = vtest_get_drm_fd
};


struct vtest_renderer renderer = {
   .max_length = UINT_MAX,
   .fence_id = 1,
};

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
   if (iovec->iov_base)
      munmap(iovec->iov_base, iovec->iov_len);
   free(iovec);
}

static int vtest_block_write(int fd, void *buf, int size)
{
   char *ptr = buf;
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

int vtest_block_read(struct vtest_input *input, void *buf, int size)
{
   int fd = input->data.fd;
   char *ptr = buf;
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

static int vtest_send_fd(int socket_fd, int fd)
{
    struct iovec iovec;
    char buf[CMSG_SPACE(sizeof(int))], c;
    struct msghdr msgh = { 0 };
    memset(buf, 0, sizeof(buf));

    iovec.iov_base = &c;
    iovec.iov_len = sizeof(char);

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iovec;
    msgh.msg_iovlen = 1;
    msgh.msg_control = buf;
    msgh.msg_controllen = sizeof(buf);
    msgh.msg_flags = 0;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    *((int *) CMSG_DATA(cmsg)) = fd;

    int size = sendmsg(socket_fd, &msgh, 0);
    if (size < 0) {
      return report_failure("Failed to send fd", -EINVAL);
    }

    return 0;
}

int vtest_buf_read(struct vtest_input *input, void *buf, int size)
{
   struct vtest_buffer *inbuf = input->data.buffer;
   if (size > inbuf->size) {
      return 0;
   }

   memcpy(buf, inbuf->buffer, size);
   inbuf->buffer += size;
   inbuf->size -= size;

   return size;
}

int vtest_create_renderer(struct vtest_input *input, int out_fd, uint32_t length,
                          int ctx_flags, const char * render_device)
{
   char *vtestname;
   int ret;

   renderer.iovec_hash = util_hash_table_create(hash_func, compare_iovecs, free_iovec);
   renderer.input = input;
   renderer.out_fd = out_fd;
   renderer.rendernode_name = render_device;

   /* By default we support version 0 unless VCMD_PROTOCOL_VERSION is sent */
   renderer.protocol_version = 0;

   ret = virgl_renderer_init(&renderer,
         ctx_flags | VIRGL_RENDERER_THREAD_SYNC, &vtest_cbs);
   if (ret) {
      fprintf(stderr, "failed to initialise renderer.\n");
      return -1;
   }

   if (length > 1024 * 1024) {
      return -1;
   }

   vtestname = calloc(1, length + 1);
   if (!vtestname) {
      return -1;
   }

   ret = renderer.input->read(renderer.input, vtestname, length);
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

   ret = renderer.input->read(renderer.input, &version_buf, sizeof(version_buf));
   if (ret != sizeof(version_buf))
      return -1;

   renderer.protocol_version = MIN2(version_buf[VCMD_PROTOCOL_VERSION_VERSION],
                                    VTEST_PROTOCOL_VERSION);

   /*
    * We've deprecated protocol version 1. All of it's called sites are being
    * moved protocol version 2. If the server supports version 2 and the guest
    * supports verison 1, fall back to version 0.
    */
   if (renderer.protocol_version == 1) {
      printf("Older guest Mesa detected, fallbacking to protocol version 0\n");
      renderer.protocol_version = 0;
   }

   /* Protocol version 2 requires shm support. */
   if (!vtest_shm_check()) {
      printf("Shared memory not supported, fallbacking to protocol version 0\n");
      renderer.protocol_version = 0;
   }

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
   util_hash_table_destroy(renderer.iovec_hash);
   renderer.iovec_hash = NULL;
   renderer.input = NULL;
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

   ret = renderer.input->read(renderer.input, &res_create_buf,
                              sizeof(res_create_buf));
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
   int ret, fd;

   ret = renderer.input->read(renderer.input, &res_create_buf,
                              sizeof(res_create_buf));
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
   if (ret)
      return report_failed_call("virgl_renderer_resource_create", ret);

   virgl_renderer_ctx_attach_resource(ctx_id, args.handle);

   iovec = CALLOC_STRUCT(iovec);
   if (!iovec) {
      return -ENOMEM;
   }

   iovec->iov_len = res_create_buf[VCMD_RES_CREATE2_DATA_SIZE];

   /* Multi-sample textures have no backing store, but an associated GL resource. */
   if (iovec->iov_len == 0) {
      iovec->iov_base = NULL;
      goto out;
   }

   fd = vtest_new_shm(args.handle, iovec->iov_len);
   if (fd < 0) {
      FREE(iovec);
      return report_failed_call("vtest_new_shm", fd);
   }

   iovec->iov_base = mmap(NULL, iovec->iov_len, PROT_WRITE | PROT_READ,
                          MAP_SHARED, fd, 0);

   if (iovec->iov_base == MAP_FAILED) {
      close(fd);
      FREE(iovec);
      return -ENOMEM;
   }

   ret = vtest_send_fd(renderer.out_fd, fd);
   if (ret < 0) {
      close(fd);
      munmap(iovec->iov_base, iovec->iov_len);
      return report_failed_call("vtest_send_fd", ret);
   }

   /* Closing the file descriptor does not unmap the region. */
   close(fd);

out:
   virgl_renderer_resource_attach_iov(args.handle, iovec, 1);
   util_hash_table_set(renderer.iovec_hash, intptr_to_pointer(args.handle), iovec);
   return 0;
}

int vtest_resource_unref(UNUSED uint32_t length_dw)
{
   uint32_t res_unref_buf[VCMD_RES_UNREF_SIZE];
   int ret;
   uint32_t handle;

   ret = renderer.input->read(renderer.input, &res_unref_buf,
                              sizeof(res_unref_buf));
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

   if (length_dw > renderer.max_length / 4) {
      return -1;
   }

   cbuf = malloc(length_dw * 4);
   if (!cbuf) {
      return -1;
   }

   ret = renderer.input->read(renderer.input, cbuf, length_dw * 4);
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

   ret = renderer.input->read(renderer.input, thdr_buf,
                              VCMD_TRANSFER_HDR_SIZE * 4);
   if (ret != VCMD_TRANSFER_HDR_SIZE * 4) {
      return ret;
   }

   DECODE_TRANSFER;

   if (data_size > renderer.max_length) {
      return -ENOMEM;
   }

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

int vtest_transfer_get_nop(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
   int ret;
   UNUSED int level;
   UNUSED uint32_t stride, layer_stride, handle;
   UNUSED struct virgl_box box;
   uint32_t data_size;
   void *ptr;

   ret = renderer.input->read(renderer.input, thdr_buf,
                              VCMD_TRANSFER_HDR_SIZE * 4);
   if (ret != VCMD_TRANSFER_HDR_SIZE * 4) {
      return ret;
   }

   DECODE_TRANSFER;

   if (data_size > renderer.max_length) {
      return -ENOMEM;
   }

   ptr = malloc(data_size);
   if (!ptr) {
      return -ENOMEM;
   }

   memset(ptr, 0, data_size);

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

   ret = renderer.input->read(renderer.input, thdr_buf,
                              VCMD_TRANSFER_HDR_SIZE * 4);
   if (ret != VCMD_TRANSFER_HDR_SIZE * 4) {
      return ret;
   }

   DECODE_TRANSFER;

   if (data_size > renderer.max_length) {
      return -ENOMEM;
   }

   ptr = malloc(data_size);
   if (!ptr) {
      return -ENOMEM;
   }

   ret = renderer.input->read(renderer.input, ptr, data_size);
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

int vtest_transfer_put_nop(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
   int ret;
   UNUSED int level;
   UNUSED uint32_t stride, layer_stride, handle;
   UNUSED struct virgl_box box;
   uint32_t data_size;
   void *ptr;

   ret = renderer.input->read(renderer.input, thdr_buf,
                              VCMD_TRANSFER_HDR_SIZE * 4);
   if (ret != VCMD_TRANSFER_HDR_SIZE * 4) {
      return ret;
   }

   DECODE_TRANSFER;

   if (data_size > renderer.max_length) {
      return -ENOMEM;
   }

   ptr = malloc(data_size);
   if (!ptr) {
      return -ENOMEM;
   }

   ret = renderer.input->read(renderer.input, ptr, data_size);
   if (ret < 0) {
      return ret;
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
      offset = thdr_buf[VCMD_TRANSFER2_OFFSET];		\
   } while(0)


int vtest_transfer_get2(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER2_HDR_SIZE];
   int ret;
   int level;
   uint32_t handle;
   struct virgl_box box;
   uint32_t offset;
   struct iovec *iovec;

   ret = renderer.input->read(renderer.input, thdr_buf, sizeof(thdr_buf));
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

   return 0;
}

int vtest_transfer_get2_nop(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER2_HDR_SIZE];
   int ret;
   UNUSED int level;
   uint32_t handle;
   UNUSED struct virgl_box box;
   uint32_t offset;
   struct iovec *iovec;

   ret = renderer.input->read(renderer.input, thdr_buf, sizeof(thdr_buf));
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

   return 0;
}

int vtest_transfer_put2(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER2_HDR_SIZE];
   int ret;
   int level;
   uint32_t handle;
   struct virgl_box box;
   UNUSED uint32_t data_size;
   uint32_t offset;
   struct iovec *iovec;

   ret = renderer.input->read(renderer.input, thdr_buf, sizeof(thdr_buf));
   if (ret != sizeof(thdr_buf)) {
      return ret;
   }

   DECODE_TRANSFER2;

   iovec = util_hash_table_get(renderer.iovec_hash, intptr_to_pointer(handle));
   if (!iovec) {
      return report_failed_call("util_hash_table_get", -ESRCH);
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

int vtest_transfer_put2_nop(UNUSED uint32_t length_dw)
{
   uint32_t thdr_buf[VCMD_TRANSFER2_HDR_SIZE];
   int ret;
   UNUSED int level;
   uint32_t handle;
   UNUSED struct virgl_box box;
   UNUSED uint32_t data_size;
   UNUSED uint32_t offset;
   struct iovec *iovec;

   ret = renderer.input->read(renderer.input, thdr_buf, sizeof(thdr_buf));
   if (ret != sizeof(thdr_buf)) {
      return ret;
   }

   DECODE_TRANSFER2;

   iovec = util_hash_table_get(renderer.iovec_hash, intptr_to_pointer(handle));
   if (!iovec) {
      return report_failed_call("util_hash_table_get", -ESRCH);
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

   ret = renderer.input->read(renderer.input, &bw_buf, sizeof(bw_buf));
   if (ret != sizeof(bw_buf)) {
      return -1;
   }

   /*  handle = bw_buf[VCMD_BUSY_WAIT_HANDLE]; unused as of now */
   flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];

   if (flags == VCMD_BUSY_WAIT_FLAG_WAIT) {
      do {
         if (renderer.last_fence == (renderer.fence_id - 1)) {
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
      busy = renderer.last_fence != (renderer.fence_id - 1);
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
   virgl_renderer_create_fence(renderer.fence_id++, ctx_id);
   return 0;
}

int vtest_poll(void)
{
   virgl_renderer_poll();
   return 0;
}

void vtest_set_max_length(uint32_t length)
{
   renderer.max_length = length;
}
