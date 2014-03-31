/* library interface from QEMU to virglrenderer */

#ifndef VIRGLRENDERER_H
#define VIRGLRENDERER_H

#include <stdint.h>

struct virgl_iovec;

#define VIRGL_EXPORT  __attribute__((visibility("default")))

typedef void *virgl_gl_context;

struct virgl_renderer_callbacks {
   int version;
   void (*write_fence)(void *cookie, uint32_t fence);

   int (*map_iov)(struct virgl_iovec *iov, uint64_t addr);
   void (*unmap_iov)(struct virgl_iovec *iov);
   
   /* interact with GL implementation */
   virgl_gl_context (*create_gl_context)(void *cookie, int scanout_idx);
   void (*destroy_gl_context)(void *cookie, virgl_gl_context ctx);
   int (*make_current)(void *cookie, int scanout_idx, virgl_gl_context ctx);

   /* */
   void (*rect_update)(void *cookie, int idx, int x, int y, int width, int height);
   void (*scanout_resource_info)(void *cookie, int idx, uint32_t tex_id, uint32_t flags,
                                 uint32_t stride, uint32_t width, uint32_t height, uint32_t format);
   void (*scanout_rect_info)(void *cookie, int idx, uint32_t tex_id,
                        int x, int y,
                        uint32_t width, uint32_t height);
};

/* virtio-gpu compatible interface */
#define VIRGL_RENDERER_USE_EGL 1

VIRGL_EXPORT int virgl_renderer_init(void *cookie, int flags, struct virgl_renderer_callbacks *cb);
VIRGL_EXPORT void virgl_renderer_poll(void); /* force fences */

VIRGL_EXPORT int virgl_renderer_process_vcmd(void *cmd, struct virgl_iovec *iov, unsigned int num_iovs);

VIRGL_EXPORT void virgl_renderer_set_cursor_info(uint32_t cursor_handle, int x, int y);

/* we need to give qemu the cursor resource contents */
VIRGL_EXPORT void *virgl_get_cursor_data(uint32_t resource_id, uint32_t *width, uint32_t *height);

VIRGL_EXPORT void virgl_renderer_get_rect(int idx, struct virgl_iovec *iov, unsigned int num_iovs,
                                          uint32_t offset, int x, int y, int width, int height);

VIRGL_EXPORT int virgl_renderer_get_fd_for_texture(uint32_t tex_id, int *fd);
#endif
