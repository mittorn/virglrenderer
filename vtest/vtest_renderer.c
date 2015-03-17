#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "virglrenderer.h"

#include <sys/uio.h>
#include "vtest.h"
#include "vtest_protocol.h"

static int ctx_id = 1;
static int fence_id = 1;

static int last_fence;
static void vtest_write_fence(void *cookie, uint32_t fence_id)
{
  last_fence = fence_id;
}

struct virgl_renderer_callbacks vtest_cbs = {
    .version = 1,
    .write_fence = vtest_write_fence,
};

struct vtest_renderer {
  int remote_fd;
};

struct vtest_renderer renderer;

struct virgl_box {
	uint32_t x, y, z;
	uint32_t w, h, d;
};

static int vtest_block_write(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   left = size;
   do {
      ret = write(fd, ptr, left);
      if (ret < 0)
         return -errno;
      left -= ret;
      ptr += ret;
   } while (left);
   return size;
}

int vtest_block_read(int fd, void *buf, int size)
{
   void *ptr = buf;
   int left;
   int ret;
   left = size;
   do {
      ret = read(fd, ptr, left);
      if (ret <= 0)
	return ret == -1 ? -errno : 0;
      left -= ret;
      ptr += ret;
   } while (left);
   return size;
}

int vtest_create_renderer(int fd, uint32_t length)
{
    char *vtestname;
    int ret;

    renderer.remote_fd = fd;

    virgl_renderer_init(&renderer, VIRGL_RENDERER_USE_EGL, &vtest_cbs);

    vtestname = malloc(length);
    if (!vtestname)
      return -1;

    ret = vtest_block_read(renderer.remote_fd, vtestname, length);
    if (ret != length)
	return -1;
    ret = virgl_renderer_context_create(ctx_id, strlen(vtestname), vtestname);
    return ret;
}

void vtest_destroy_renderer(void)
{
  virgl_renderer_context_destroy(ctx_id);
  virgl_renderer_cleanup(&renderer);
  renderer.remote_fd = 0;
}

int vtest_send_caps(void)
{
    uint32_t  max_ver, max_size;
    void *caps_buf;
    uint32_t hdr_buf[2];
    int ret;

    virgl_renderer_get_cap_set(1, &max_ver, &max_size);

    caps_buf = malloc(max_size);
    if (!caps_buf)
	return -1;
    
    virgl_renderer_fill_caps(1, 1, caps_buf);

    hdr_buf[0] = max_size + 1;
    hdr_buf[1] = 1;
    ret = vtest_block_write(renderer.remote_fd, hdr_buf, 8);
    if (ret < 0)
      return ret;
    vtest_block_write(renderer.remote_fd, caps_buf, max_size);
    if (ret < 0)
      return ret;

    return 0;
}

int vtest_create_resource(void)
{
    uint32_t res_create_buf[VCMD_RES_CREATE_SIZE];
    struct virgl_renderer_resource_create_args args;
    int ret;

    ret = vtest_block_read(renderer.remote_fd, &res_create_buf, sizeof(res_create_buf));
    if (ret != sizeof(res_create_buf))
	return -1;
	
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

int vtest_resource_unref(void)
{
    uint32_t res_unref_buf[VCMD_RES_UNREF_SIZE];
    int ret;
    uint32_t handle;

    ret = vtest_block_read(renderer.remote_fd, &res_unref_buf, sizeof(res_unref_buf));
    if (ret != sizeof(res_unref_buf))
      return -1;

    handle = res_unref_buf[VCMD_RES_UNREF_RES_HANDLE];
    virgl_renderer_ctx_attach_resource(ctx_id, handle);
    virgl_renderer_resource_unref(handle);
    return 0;
}

int vtest_submit_cmd(uint32_t length_dw)
{
    uint32_t *cbuf;
    int ret;

    cbuf = malloc(length_dw * 4);
    if (!cbuf)
	return -1;

    ret = vtest_block_read(renderer.remote_fd, cbuf, length_dw * 4);
    if (ret != length_dw * 4)
	return -1;

    virgl_renderer_submit_cmd(cbuf, ctx_id, length_dw);

    free(cbuf);
    return 0;
}

#define DECODE_TRANSFER \
  do {							\
  handle = thdr_buf[VCMD_TRANSFER_RES_HANDLE];		\
  level = thdr_buf[VCMD_TRANSFER_LEVEL];		\
  stride = thdr_buf[VCMD_TRANSFER_STRIDE];		\
  layer_stride = thdr_buf[VCMD_TRANSFER_LAYER_STRIDE];	\
  box.x = thdr_buf[VCMD_TRANSFER_X];			\
  box.y = thdr_buf[VCMD_TRANSFER_Y];			\
  box.z = thdr_buf[VCMD_TRANSFER_Z];			\
  box.w = thdr_buf[VCMD_TRANSFER_WIDTH];		\
  box.h = thdr_buf[VCMD_TRANSFER_HEIGHT];		\
  box.d = thdr_buf[VCMD_TRANSFER_DEPTH];		\
  data_size = thdr_buf[VCMD_TRANSFER_DATA_SIZE];		\
  } while(0)


int vtest_transfer_get(uint32_t length_dw)
{
    uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
    int ret;
    int level;
    uint32_t stride, layer_stride, handle;
    struct virgl_box box;
    uint32_t data_size;
    void *ptr;
    struct iovec iovec;

    ret = vtest_block_read(renderer.remote_fd, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
    if (ret != VCMD_TRANSFER_HDR_SIZE * 4)
      return ret;

    DECODE_TRANSFER;

    ptr = malloc(data_size);
    if (!ptr)
      return -ENOMEM;

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
    if (ret)
      fprintf(stderr," transfer read failed %d\n", ret);
    ret = vtest_block_write(renderer.remote_fd, ptr, data_size);
    if (ret < 0)
      return ret;

    free(ptr);
    return 0;
}

int vtest_transfer_put(uint32_t length_dw)
{
    uint32_t thdr_buf[VCMD_TRANSFER_HDR_SIZE];
    int ret;
    int level;
    uint32_t stride, layer_stride, handle;
    struct virgl_box box;
    uint32_t data_size;
    void *ptr;
    struct iovec iovec;

    ret = vtest_block_read(renderer.remote_fd, thdr_buf, VCMD_TRANSFER_HDR_SIZE * 4);
    if (ret != VCMD_TRANSFER_HDR_SIZE * 4)
      return ret;

    DECODE_TRANSFER;

    ptr = malloc(data_size);
    if (!ptr)
      return -ENOMEM;

    ret = vtest_block_read(renderer.remote_fd, ptr, data_size);
    if (ret < 0)
      return ret;

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
    if (ret)
      fprintf(stderr," transfer write failed %d\n", ret);
    free(ptr);
    return 0;
}

int vtest_resource_busy_wait(void)
{
  uint32_t bw_buf[VCMD_BUSY_WAIT_SIZE];
  int ret;
  int flags;
  uint32_t hdr_buf[VTEST_HDR_SIZE];
  uint32_t reply_buf[1];
  bool busy = false;
  ret = vtest_block_read(renderer.remote_fd, &bw_buf, sizeof(bw_buf));
  if (ret != sizeof(bw_buf))
    return -1;

  /*  handle = bw_buf[VCMD_BUSY_WAIT_HANDLE]; unused as of now */
  flags = bw_buf[VCMD_BUSY_WAIT_FLAGS];

  if (flags == VCMD_BUSY_WAIT_FLAG_WAIT) {
    do {
      if (last_fence != (fence_id - 1))
	virgl_renderer_poll();
      else
	break;
    } while (1);
    busy = false;
  } else {
    busy = last_fence != (fence_id - 1);
  }

  hdr_buf[VTEST_CMD_LEN] = 1;
  hdr_buf[VTEST_CMD_ID] = VCMD_RESOURCE_BUSY_WAIT;
  reply_buf[0] = busy ? 1 : 0;

  ret = vtest_block_write(renderer.remote_fd, hdr_buf, sizeof(hdr_buf));
  if (ret < 0)
    return ret;

  ret = vtest_block_write(renderer.remote_fd, reply_buf, sizeof(reply_buf));
  if (ret < 0)
    return ret;

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
