#ifndef GRAW_RENDERER_H
#define GRAW_RENDERER_H

#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "virgl_protocol.h"
#include "graw_iov.h"
#include "virgl_hw.h"

typedef void *virgl_gl_context;
typedef void *virgl_gl_drawable;

extern int vrend_dump_shaders;
struct grend_context;

struct grend_resource {
   struct pipe_resource base;
   GLuint id;
   GLenum target;
   /* fb id if we need to readback this resource */
   GLuint readback_fb_id;
   GLuint readback_fb_level;
   GLuint readback_fb_z;
   int is_front;
   GLboolean renderer_flipped;
   void *ptr;
   GLuint handle;

   struct virgl_iovec *iov;
   uint32_t num_iovs;
   boolean y_0_top;

   boolean scannedout;
};

/* assume every format is sampler friendly */
#define VREND_BIND_RENDER (1 << 0)
#define VREND_BIND_DEPTHSTENCIL (1 << 1)

struct grend_format_table {
   enum virgl_formats format;
   GLenum internalformat;
   GLenum glformat;
   GLenum gltype;
   uint32_t bindings;
};

struct grend_if_cbs {
   void (*write_fence)(unsigned fence_id);
   /* inform the control layer about a new scanout */
   void (*scanout_rect_info)(int scanout_id, GLuint tex_id, int x, int y,
                             uint32_t width, uint32_t height);
   void (*scanout_resource_info)(int scanout_id, GLuint tex_id, uint32_t flags,
                                 uint32_t stride,
                                 uint32_t width, uint32_t height, uint32_t format);

   virgl_gl_context (*create_gl_context)(int scanout);
   void (*destroy_gl_context)(virgl_gl_context ctx);
   int (*make_current)(int scanout, virgl_gl_context ctx);

   void (*flush_scanout)(int scanout, int x, int y, uint32_t width, uint32_t height);
   void (*inval_backing)(struct virgl_iovec *iov, uint32_t iov_cnt);
};
void graw_renderer_init(struct grend_if_cbs *cbs);

void grend_insert_format(struct grend_format_table *entry, uint32_t bindings);
void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs);

void grend_create_fs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs);

void grend_bind_vs(struct grend_context *ctx,
                   uint32_t handle);

void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle);

void grend_bind_vs_so(struct grend_context *ctx,
                      uint32_t handle);
void grend_clear(struct grend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil);

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info);

void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
   uint32_t zsurf_handle);

void grend_flush(struct grend_context *ctx);


void grend_flush_frontbuffer(uint32_t res_handle);
struct grend_context *grend_create_context(int id, uint32_t nlen, const char *debug_name);
bool grend_destroy_context(struct grend_context *ctx);
void graw_renderer_context_create(uint32_t handle, uint32_t nlen, const char *name);
void graw_renderer_context_create_internal(uint32_t handle, uint32_t nlen, const char *name);
void graw_renderer_context_destroy(uint32_t handle);

struct graw_renderer_resource_create_args {
   uint32_t handle;
   enum pipe_texture_target target;
   uint32_t format;
   uint32_t bind;
   uint32_t width;
   uint32_t height;
   uint32_t depth;
   uint32_t array_size;
   uint32_t last_level;
   uint32_t nr_samples;
   uint32_t flags;
};
     
void graw_renderer_resource_create(struct graw_renderer_resource_create_args *args, struct virgl_iovec *iov, uint32_t num_iovs);

void graw_renderer_resource_unref(uint32_t handle);

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle, uint32_t format,
                          uint32_t val0, uint32_t val1);
void grend_create_sampler_view(struct grend_context *ctx,
                               uint32_t handle,
                               uint32_t res_handle, uint32_t format,
                               uint32_t val0, uint32_t val1, uint32_t swizzle_packed);

void grend_create_so_target(struct grend_context *ctx,
                            uint32_t handle,
                            uint32_t res_handle,
                            uint32_t buffer_offset,
                            uint32_t buffer_size);
void grend_set_streamout_targets(struct grend_context *ctx,
                                 uint32_t append_bitmask,
                                 uint32_t num_targets,
                                 uint32_t *handles);

void grend_create_vertex_elements_state(struct grend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements);
void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle);

void grend_set_single_vbo(struct grend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle);
void grend_set_num_vbo(struct grend_context *ctx,
                      int num_vbo);

void grend_transfer_inline_write(struct grend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride);

void grend_set_viewport_state(struct grend_context *ctx,
                              const struct pipe_viewport_state *state);
void grend_set_num_sampler_views(struct grend_context *ctx,
                                 uint32_t shader_type,
                                 uint32_t start_slot,
                                 int num_sampler_views);
void grend_set_single_sampler_view(struct grend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t res_handle);

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle);
void grend_object_bind_dsa(struct grend_context *ctx,
                             uint32_t handle);
void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle);

void grend_bind_sampler_states(struct grend_context *ctx,
                               uint32_t shader_type,
                               uint32_t start_slot,
                               uint32_t num_states,
                               uint32_t *handles);
void grend_set_index_buffer(struct grend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset);

void graw_renderer_transfer_write_iov(uint32_t handle, 
                                      uint32_t ctx_id,
                                      int level,
                                      uint32_t stride,
                                      uint32_t layer_stride,
                                      struct pipe_box *box,
                                      uint64_t offset,
                                      struct virgl_iovec *iovec,
                                      unsigned int iovec_cnt);

void graw_renderer_resource_copy_region(struct grend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box);

void graw_renderer_blit(struct grend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info);

void graw_renderer_transfer_send_iov(uint32_t handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct pipe_box *box,
                                     uint64_t offset, struct virgl_iovec *iov,
                                     int iovec_cnt);
void grend_set_stencil_ref(struct grend_context *ctx, struct pipe_stencil_ref *ref);
void grend_set_blend_color(struct grend_context *ctx, struct pipe_blend_color *color);
void grend_set_scissor_state(struct grend_context *ctx, struct pipe_scissor_state *ss);

void grend_set_polygon_stipple(struct grend_context *ctx, struct pipe_poly_stipple *ps);

void grend_set_clip_state(struct grend_context *ctx, struct pipe_clip_state *ucp);
void grend_set_sample_mask(struct grend_context *ctx, unsigned sample_mask);

void grend_set_constants(struct grend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data);

void graw_transfer_write_return(void *data, uint32_t bytes, uint64_t offset,
                                struct virgl_iovec *iov, int iovec_cnt);

void graw_transfer_write_tex_return(struct pipe_resource *res,
				    struct pipe_box *box,
                                    uint32_t level,
                                    uint32_t dst_stride,
                                    uint64_t offset,
                                    struct virgl_iovec *iov,
                                    int num_iovs,
				    void *myptr, int size, int invert);

int graw_renderer_set_scanout(uint32_t res_handle,
                              uint32_t scanout_id,
                              uint32_t ctx_id,
                              struct pipe_box *box);

int graw_renderer_flush_buffer(uint32_t res_handle,
                               uint32_t ctx_id,
                               struct pipe_box *box);

void graw_renderer_fini(void);
void graw_reset_decode(void);

void graw_decode_block_iov(struct virgl_iovec *iov, uint32_t niovs, uint32_t ctx_id, uint64_t offset, int ndw);
struct grend_context *vrend_lookup_renderer_ctx(uint32_t ctx_id);

int graw_renderer_create_fence(int client_fence_id, uint32_t ctx_id);

void graw_renderer_check_fences(void);
void graw_renderer_check_queries(void);
void grend_stop_current_queries(void);

boolean grend_hw_switch_context(struct grend_context *ctx, boolean now);
void graw_renderer_object_insert(struct grend_context *ctx, void *data,
                                 uint32_t size, uint32_t handle, enum virgl_object_type type);
void graw_renderer_object_destroy(struct grend_context *ctx, uint32_t handle);

void grend_create_query(struct grend_context *ctx, uint32_t handle,
                        uint32_t query_type, uint32_t res_handle,
                        uint32_t offset);

void grend_begin_query(struct grend_context *ctx, uint32_t handle);
void grend_end_query(struct grend_context *ctx, uint32_t handle);
void grend_get_query_result(struct grend_context *ctx, uint32_t handle,
                            uint32_t wait);
void grend_set_query_state(struct grend_context *ctx,
                           boolean enabled);
void grend_render_condition(struct grend_context *ctx,
                            uint32_t handle,
                            boolean condtion,
                            uint mode);
void grend_set_cursor_info(uint32_t cursor_handle, int x, int y);
void *graw_renderer_get_cursor_contents(uint32_t res_handle, uint32_t *width, uint32_t *height);
void grend_use_program(GLuint program_id);
void grend_blend_enable(GLboolean blend_enable);
void grend_depth_test_enable(GLboolean depth_test_enable);
void grend_bind_va(GLuint vaoid);
int graw_renderer_flush_buffer_res(struct grend_resource *res,
                                   struct pipe_box *box);

void graw_renderer_fill_caps(uint32_t set, uint32_t version,
                             union virgl_caps *caps);

GLint64 graw_renderer_get_timestamp(void);
/* formats */
void vrend_build_format_list(void);

int graw_renderer_resource_attach_iov(int res_handle, struct virgl_iovec *iov,
                                       int num_iovs);
void graw_renderer_resource_invalid_iov(int res_handle);
void graw_renderer_resource_destroy(struct grend_resource *res);

static INLINE void
grend_resource_reference(struct grend_resource **ptr, struct grend_resource *tex)
{
   struct grend_resource *old_tex = *ptr;

   if (pipe_reference(&(*ptr)->base.reference, &tex->base.reference))
      graw_renderer_resource_destroy(old_tex);
   *ptr = tex;
}

void graw_renderer_force_ctx_0(void);

void graw_renderer_get_rect(int idx, struct virgl_iovec *iov, unsigned int num_iovs,
                            uint32_t offset, int x, int y, int width, int height);
#endif
