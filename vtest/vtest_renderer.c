#include <stdlib.h>
#include <unistd.h>
#include "virglrenderer.h"

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
    renderer.remote_fd = fd;
    virgl_renderer_init(&renderer, VIRGL_RENDERER_USE_EGL, &vtest_cbs);
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
