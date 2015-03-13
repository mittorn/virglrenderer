#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "virglrenderer.h"

#include "vtest_protocol.h"

static int ctx_id = 1;
struct virgl_renderer_callbacks vtest_cbs = {
    .version = 1,
    //    .write_fence = vtest_write_fence,
};

struct vtest_renderer {
  int remote_fd;
};

struct vtest_renderer renderer;

int vtest_create_renderer(int fd)
{
    const char *vtestname = "vtestname";
    int ret;

    renderer.remote_fd = fd;
    virgl_renderer_init(&renderer, VIRGL_RENDERER_USE_EGL, &vtest_cbs);

    ret = virgl_renderer_context_create(ctx_id, strlen(vtestname), vtestname);
    return ret;
}

int vtest_send_caps(void)
{
    uint32_t  max_ver, max_size;
    void *caps_buf;
    uint32_t hdr_buf[2];
    virgl_renderer_get_cap_set(1, &max_ver, &max_size);

    caps_buf = malloc(max_size);
    if (!caps_buf)
	return -1;
    
    virgl_renderer_fill_caps(1, 1, caps_buf);

    hdr_buf[0] = max_size + 1;
    hdr_buf[1] = 1;
    write(renderer.remote_fd, hdr_buf, 8);
    write(renderer.remote_fd, caps_buf, max_size);
    return 0;
}

int vtest_create_resource(void)
{
    uint32_t res_create_buf[VCMD_RES_CREATE_SIZE];
    struct virgl_renderer_resource_create_args args;
    int ret;

    ret = read(renderer.remote_fd, &res_create_buf, sizeof(res_create_buf));
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

    ret = read(renderer.remote_fd, &res_unref_buf, sizeof(res_unref_buf));
    if (ret != sizeof(res_unref_buf))
      return -1;

    handle = res_unref_buf[VCMD_RES_UNREF_RES_HANDLE];
    virgl_renderer_ctx_attach_resource(ctx_id, handle);
    virgl_renderer_resource_unref(handle);
    return 0;
}
