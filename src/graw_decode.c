#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <epoxy/gl.h>

#include "util/u_memory.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"
#include "graw_decode.h"
#include "graw_renderer.h"
#include "vrend_object.h"
#include "tgsi/tgsi_text.h"

/* decode side */
#define DECODE_MAX_TOKENS 8000

struct grend_decode_ctx {
   struct graw_decoder_state ids, *ds;
   struct grend_context *grctx;
};

#define GRAW_MAX_CTX 16
static struct grend_decode_ctx *dec_ctx[GRAW_MAX_CTX];

static int graw_decode_create_shader(struct grend_decode_ctx *ctx, uint32_t type,
                              uint32_t handle,
   uint16_t length)
{
   struct pipe_shader_state *state = CALLOC_STRUCT(pipe_shader_state);
   struct tgsi_token *tokens;
   int i;
   uint32_t shader_offset;
   unsigned num_tokens;
   if (!state)
      return NULL;
   
   num_tokens = ctx->ds->buf[ctx->ds->buf_offset + 2];

   if (num_tokens == 0)
      num_tokens = 300;

   tokens = calloc(num_tokens + 10, sizeof(struct tgsi_token));
   if (!tokens) {
      free(state);
      return -1;
   }

   state->stream_output.num_outputs = ctx->ds->buf[ctx->ds->buf_offset + 3];
   if (state->stream_output.num_outputs) {
      for (i = 0; i < 4; i++)
         state->stream_output.stride[i] = ctx->ds->buf[ctx->ds->buf_offset + 4 + i];
      for (i = 0; i < state->stream_output.num_outputs; i++) {
         uint32_t tmp = ctx->ds->buf[ctx->ds->buf_offset + 8 + i];
      
         state->stream_output.output[i].register_index = tmp & 0xff;
         state->stream_output.output[i].start_component = (tmp >> 8) & 0x3;
         state->stream_output.output[i].num_components = (tmp >> 10) & 0x7;
         state->stream_output.output[i].output_buffer = (tmp >> 13) & 0x7;
         state->stream_output.output[i].dst_offset = (tmp >> 16) & 0xffff;         
      }
      shader_offset = 8 + state->stream_output.num_outputs;
   } else
      shader_offset = 4;
   if (vrend_dump_shaders)
      fprintf(stderr,"shader\n%s\n", &ctx->ds->buf[ctx->ds->buf_offset + shader_offset]);
   if (!tgsi_text_translate(&ctx->ds->buf[ctx->ds->buf_offset + shader_offset], tokens, num_tokens + 10)) {
      fprintf(stderr,"failed to translate\n %s\n",&ctx->ds->buf[ctx->ds->buf_offset + shader_offset]);
      free(tokens);
      free(state);
      return -1;
   }

   state->tokens = tokens;

   if (type == VIRGL_OBJECT_FS)
      grend_create_fs(ctx->grctx, handle, state);
   else
      grend_create_vs(ctx->grctx, handle, state);

   free(tokens);
   free(state);
   return 0;
}

static int graw_decode_create_stream_output_target(struct grend_decode_ctx *ctx, uint32_t handle)
{
   uint32_t res_handle, buffer_size, buffer_offset;

   res_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];
   buffer_offset = ctx->ds->buf[ctx->ds->buf_offset + 3];
   buffer_size = ctx->ds->buf[ctx->ds->buf_offset + 4];

   grend_create_so_target(ctx->grctx, handle, res_handle, buffer_offset,
                          buffer_size);
}

static void graw_decode_set_framebuffer_state(struct grend_decode_ctx *ctx)
{
   uint32_t nr_cbufs = ctx->ds->buf[ctx->ds->buf_offset + 1];
   uint32_t zsurf_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];
   uint32_t surf_handle[8];
   int i;

   for (i = 0; i < nr_cbufs; i++)
      surf_handle[i] = ctx->ds->buf[ctx->ds->buf_offset + 3 + i];
   grend_set_framebuffer_state(ctx->grctx, nr_cbufs, surf_handle, zsurf_handle);
}

static void graw_decode_clear(struct grend_decode_ctx *ctx)
{
   union pipe_color_union color;
   double depth;
   unsigned stencil, buffers;
   int i;
   int index = ctx->ds->buf_offset + 1;
   
   buffers = ctx->ds->buf[index++];
   for (i = 0; i < 4; i++)
      color.ui[i] = ctx->ds->buf[index++];
   depth = *(double *)(uint64_t *)(&ctx->ds->buf[index]);
   index += 2;
   stencil = ctx->ds->buf[index++];

   grend_clear(ctx->grctx, buffers, &color, depth, stencil);
}

static float uif(unsigned int ui)
{
   union { float f; unsigned int ui; } myuif;
   myuif.ui = ui;
   return myuif.f;
}

static void graw_decode_set_viewport_state(struct grend_decode_ctx *ctx)
{
   struct pipe_viewport_state vps;
   int i;

   for (i = 0; i < 4; i++)
      vps.scale[i] = uif(ctx->ds->buf[ctx->ds->buf_offset + 1 + i]);
   for (i = 0; i < 4; i++)
      vps.translate[i] = uif(ctx->ds->buf[ctx->ds->buf_offset + 5 + i]);
   
   grend_set_viewport_state(ctx->grctx, &vps);
}

static void graw_decode_set_index_buffer(struct grend_decode_ctx *ctx)
{
   int offset = ctx->ds->buf_offset;
   grend_set_index_buffer(ctx->grctx, ctx->ds->buf[offset + 1],
                          ctx->ds->buf[offset + 2],
                          ctx->ds->buf[offset + 3]);
}

static void graw_decode_set_constant_buffer(struct grend_decode_ctx *ctx, uint16_t length)
{
   int offset = ctx->ds->buf_offset;
   uint32_t shader = ctx->ds->buf[offset + 1];
   uint32_t index = ctx->ds->buf[offset + 2];
   int nc = (length - 2);
   grend_set_constants(ctx->grctx, shader, index, nc, &ctx->ds->buf[offset + 3]);
}

static void graw_decode_set_vertex_buffers(struct grend_decode_ctx *ctx, uint16_t length)
{
   int num_vbo;
   int i;
   num_vbo = (length / 3);

   for (i = 0; i < num_vbo; i++) {
      int element_offset = ctx->ds->buf_offset + 1 + (i * 3);
      grend_set_single_vbo(ctx->grctx, i,
                           ctx->ds->buf[element_offset],
                           ctx->ds->buf[element_offset + 1],
                           ctx->ds->buf[element_offset + 2]);
   }
   grend_set_num_vbo(ctx->grctx, num_vbo);
}

static void graw_decode_set_sampler_views(struct grend_decode_ctx *ctx, uint16_t length)
{
   int num_samps;
   int i;
   uint32_t shader_type, start_slot;
   num_samps = length - 2;
   shader_type = ctx->ds->buf[ctx->ds->buf_offset + 1];
   start_slot = ctx->ds->buf[ctx->ds->buf_offset + 2];
   for (i = 0; i < num_samps; i++) {
      uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 3 + i];
      grend_set_single_sampler_view(ctx->grctx, shader_type, i + start_slot, handle);
   }
   grend_set_num_sampler_views(ctx->grctx, shader_type, start_slot, num_samps);
}

static void graw_decode_resource_inline_write(struct grend_decode_ctx *ctx, uint16_t length)
{
   struct pipe_box box;
   uint32_t res_handle = ctx->ds->buf[ctx->ds->buf_offset + 1];
   uint32_t level, usage, stride, layer_stride;
   void *data;

   level = ctx->ds->buf[ctx->ds->buf_offset + 2];
   usage = ctx->ds->buf[ctx->ds->buf_offset + 3];
   stride = ctx->ds->buf[ctx->ds->buf_offset + 4];
   layer_stride = ctx->ds->buf[ctx->ds->buf_offset + 5];
   box.x = ctx->ds->buf[ctx->ds->buf_offset + 6];
   box.y = ctx->ds->buf[ctx->ds->buf_offset + 7];
   box.z = ctx->ds->buf[ctx->ds->buf_offset + 8];
   box.width = ctx->ds->buf[ctx->ds->buf_offset + 9];
   box.height = ctx->ds->buf[ctx->ds->buf_offset + 10];
   box.depth = ctx->ds->buf[ctx->ds->buf_offset + 11];

   data = &ctx->ds->buf[ctx->ds->buf_offset + 12];
   grend_transfer_inline_write(ctx->grctx, res_handle, level,
                               usage, &box, data, stride, layer_stride);
                               
}

static void graw_decode_draw_vbo(struct grend_decode_ctx *ctx)
{
   struct pipe_draw_info info;

   memset(&info, 0, sizeof(struct pipe_draw_info));

   info.start = ctx->ds->buf[ctx->ds->buf_offset + 1];
   info.count = ctx->ds->buf[ctx->ds->buf_offset + 2];
   info.mode = ctx->ds->buf[ctx->ds->buf_offset + 3];
   info.indexed = ctx->ds->buf[ctx->ds->buf_offset + 4];
   info.instance_count = ctx->ds->buf[ctx->ds->buf_offset + 5];
   info.index_bias = ctx->ds->buf[ctx->ds->buf_offset + 6];
   info.start_instance = ctx->ds->buf[ctx->ds->buf_offset + 7];
   info.primitive_restart = ctx->ds->buf[ctx->ds->buf_offset + 8];
   info.restart_index = ctx->ds->buf[ctx->ds->buf_offset + 9];
   info.min_index = ctx->ds->buf[ctx->ds->buf_offset + 10];
   info.max_index = ctx->ds->buf[ctx->ds->buf_offset + 11];
   grend_draw_vbo(ctx->grctx, &info);
}

static void graw_decode_create_blend(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_blend_state *blend_state = CALLOC_STRUCT(pipe_blend_state);
   uint32_t tmp;
   int i;
   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   blend_state->independent_blend_enable = (tmp & 1);
   blend_state->logicop_enable = (tmp >> 1) & 0x1;
   blend_state->dither = (tmp >> 2) & 0x1;
   blend_state->alpha_to_coverage = (tmp >> 3) & 0x1;
   blend_state->alpha_to_one = (tmp >> 4) & 0x1;

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 3];
   blend_state->logicop_func = tmp & 0xf;

   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      tmp = ctx->ds->buf[ctx->ds->buf_offset + 4 + i];
      blend_state->rt[i].blend_enable = tmp & 0x1;
      blend_state->rt[i].rgb_func = (tmp >> 1) & 0x7;
      blend_state->rt[i].rgb_src_factor = (tmp >> 4) & 0x1f;
      blend_state->rt[i].rgb_dst_factor = (tmp >> 9) & 0x1f;
      blend_state->rt[i].alpha_func = (tmp >> 14) & 0x7;
      blend_state->rt[i].alpha_src_factor = (tmp >> 17) & 0x1f;
      blend_state->rt[i].alpha_dst_factor = (tmp >> 22) & 0x1f;
      blend_state->rt[i].colormask = (tmp >> 27) & 0xf;
   }

   graw_renderer_object_insert(ctx->grctx, blend_state, sizeof(struct pipe_blend_state), handle,
                      VIRGL_OBJECT_BLEND);
}

static void graw_decode_create_dsa(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   int i;
   struct pipe_depth_stencil_alpha_state *dsa_state = CALLOC_STRUCT(pipe_depth_stencil_alpha_state);
   uint32_t tmp;
   
   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   dsa_state->depth.enabled = tmp & 0x1;
   dsa_state->depth.writemask = (tmp >> 1) & 0x1;
   dsa_state->depth.func = (tmp >> 2) & 0x7;

   dsa_state->alpha.enabled = (tmp >> 8) & 0x1;
   dsa_state->alpha.func = (tmp >> 9) & 0x7;

   for (i = 0; i < 2; i++) {
      tmp = ctx->ds->buf[ctx->ds->buf_offset + 3 + i];
      dsa_state->stencil[i].enabled = tmp & 0x1;
      dsa_state->stencil[i].func = (tmp >> 1) & 0x7;
      dsa_state->stencil[i].fail_op = (tmp >> 4) & 0x7;
      dsa_state->stencil[i].zpass_op = (tmp >> 7) & 0x7;
      dsa_state->stencil[i].zfail_op = (tmp >> 10) & 0x7;
      dsa_state->stencil[i].valuemask = (tmp >> 13) & 0xff;
      dsa_state->stencil[i].writemask = (tmp >> 21) & 0xff;
   }

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 5];
   dsa_state->alpha.ref_value = uif(tmp);

   graw_renderer_object_insert(ctx->grctx, dsa_state, sizeof(struct pipe_depth_stencil_alpha_state), handle,
                      VIRGL_OBJECT_DSA);
}

static void graw_decode_create_rasterizer(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_rasterizer_state *rs_state = CALLOC_STRUCT(pipe_rasterizer_state);
   uint32_t tmp;

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
#define ebit(name, bit) rs_state->name = (tmp >> bit) & 0x1
#define emask(name, bit, mask) rs_state->name = (tmp >> bit) & mask

   ebit(flatshade, 0);
   ebit(depth_clip, 1);
   ebit(clip_halfz, 2);
   ebit(rasterizer_discard, 3);
   ebit(flatshade_first, 4);
   ebit(light_twoside, 5);
   ebit(sprite_coord_mode, 6);
   ebit(point_quad_rasterization, 7);
   emask(cull_face, 8, 0x3);
   emask(fill_front, 10, 0x3);
   emask(fill_back, 12, 0x3);
   ebit(scissor, 14);
   ebit(front_ccw, 15);
   ebit(clamp_vertex_color, 16);
   ebit(clamp_fragment_color, 17);
   ebit(offset_line, 18);
   ebit(offset_point, 19);
   ebit(offset_tri, 20);
   ebit(poly_smooth, 21);
   ebit(poly_stipple_enable, 22);
   ebit(point_smooth, 23);
   ebit(point_size_per_vertex, 24);
   ebit(multisample, 25);
   ebit(line_smooth, 26);
   ebit(line_stipple_enable, 27);
   ebit(line_last_pixel, 28);
   ebit(half_pixel_center, 29);
   ebit(bottom_edge_rule, 30);
   rs_state->point_size = uif(ctx->ds->buf[ctx->ds->buf_offset + 3]);
   rs_state->sprite_coord_enable = ctx->ds->buf[ctx->ds->buf_offset + 4];
   tmp = ctx->ds->buf[ctx->ds->buf_offset + 5];
   emask(line_stipple_pattern, 0, 0xffff);
   emask(line_stipple_factor, 16, 0xff);
   emask(clip_plane_enable, 24, 0xff);

   rs_state->line_width = uif(ctx->ds->buf[ctx->ds->buf_offset + 6]);
   rs_state->offset_units = uif(ctx->ds->buf[ctx->ds->buf_offset + 7]);
   rs_state->offset_scale = uif(ctx->ds->buf[ctx->ds->buf_offset + 8]);
   rs_state->offset_clamp = uif(ctx->ds->buf[ctx->ds->buf_offset + 9]);
   
   
   graw_renderer_object_insert(ctx->grctx, rs_state, sizeof(struct pipe_rasterizer_state), handle,
                      VIRGL_OBJECT_RASTERIZER);
}

static void graw_decode_create_surface(struct grend_decode_ctx *ctx, uint32_t handle)
{
   uint32_t res_handle, format, val0, val1;
   res_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];
   format = ctx->ds->buf[ctx->ds->buf_offset + 3];
   val0 = ctx->ds->buf[ctx->ds->buf_offset + 4];
   val1 = ctx->ds->buf[ctx->ds->buf_offset + 5];
   grend_create_surface(ctx->grctx, handle, res_handle, format, val0, val1);
}

static void graw_decode_create_sampler_view(struct grend_decode_ctx *ctx, uint32_t handle)
{
   uint32_t res_handle, format, val0, val1, swizzle_packed;

   res_handle = ctx->ds->buf[ctx->ds->buf_offset + 2];
   format = ctx->ds->buf[ctx->ds->buf_offset + 3];
   val0 = ctx->ds->buf[ctx->ds->buf_offset + 4];
   val1 = ctx->ds->buf[ctx->ds->buf_offset + 5];
   swizzle_packed = ctx->ds->buf[ctx->ds->buf_offset + 6];
   grend_create_sampler_view(ctx->grctx, handle, res_handle, format, val0, val1,swizzle_packed);
}

static void graw_decode_create_sampler_state(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_sampler_state *state = CALLOC_STRUCT(pipe_sampler_state);
   int i;
   uint32_t tmp;

   tmp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   state->wrap_s = tmp & 0x7;
   state->wrap_t = (tmp >> 3) & 0x7;
   state->wrap_r = (tmp >> 6) & 0x7;
   state->min_img_filter = (tmp >> 9) & 0x3;
   state->min_mip_filter = (tmp >> 11) & 0x3;
   state->mag_img_filter = (tmp >> 13) & 0x3;
   state->compare_mode = (tmp >> 15) & 0x1;
   state->compare_func = (tmp >> 16) & 0x7;

   state->lod_bias = uif(ctx->ds->buf[ctx->ds->buf_offset + 3]);
   state->min_lod = uif(ctx->ds->buf[ctx->ds->buf_offset + 4]);
   state->max_lod = uif(ctx->ds->buf[ctx->ds->buf_offset + 5]);

   for (i = 0; i < 4; i++)
      state->border_color.ui[i] = ctx->ds->buf[ctx->ds->buf_offset + 6 + i];
   graw_renderer_object_insert(ctx->grctx, state, sizeof(struct pipe_sampler_state), handle,
                      VIRGL_OBJECT_SAMPLER_STATE);
}

static void graw_decode_create_ve(struct grend_decode_ctx *ctx, uint32_t handle, uint16_t length)
{
   struct pipe_vertex_element *ve;
   int num_elements;
   int i;

   num_elements = (length - 1) / 4;
   ve = calloc(num_elements, sizeof(struct pipe_vertex_element));
   if (!ve)
      return;
      
   for (i = 0; i < num_elements; i++) {
      uint32_t element_offset = ctx->ds->buf_offset + 2 + (i * 4);
      ve[i].src_offset = ctx->ds->buf[element_offset];
      ve[i].instance_divisor = ctx->ds->buf[element_offset + 1];
      ve[i].vertex_buffer_index = ctx->ds->buf[element_offset + 2];
      ve[i].src_format = ctx->ds->buf[element_offset + 3];
   }

   grend_create_vertex_elements_state(ctx->grctx, handle, num_elements,
                                      ve);
}

static void graw_decode_create_query(struct grend_decode_ctx *ctx, uint32_t handle)
{
   uint32_t query_type;
   uint32_t res_handle;
   uint32_t offset;
   query_type = ctx->ds->buf[ctx->ds->buf_offset + 2];
   offset = ctx->ds->buf[ctx->ds->buf_offset + 3];
   res_handle = ctx->ds->buf[ctx->ds->buf_offset + 4];

   grend_create_query(ctx->grctx, handle, query_type, res_handle, offset);
}

static void graw_decode_create_object(struct grend_decode_ctx *ctx)
{
   uint32_t header = ctx->ds->buf[ctx->ds->buf_offset];
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset+1];
   uint16_t length;
   uint8_t obj_type = (header >> 8) & 0xff;

   length = header >> 16;

   switch (obj_type){
   case VIRGL_OBJECT_BLEND:
      graw_decode_create_blend(ctx, handle, length);
      break;
   case VIRGL_OBJECT_DSA:
      graw_decode_create_dsa(ctx, handle, length);
      break;
   case VIRGL_OBJECT_RASTERIZER:
      graw_decode_create_rasterizer(ctx, handle, length);
      break;
   case VIRGL_OBJECT_VS:
   case VIRGL_OBJECT_FS:
      graw_decode_create_shader(ctx, obj_type, handle, length);
      break;
   case VIRGL_OBJECT_VERTEX_ELEMENTS:
      graw_decode_create_ve(ctx, handle, length);
      break;
   case VIRGL_OBJECT_SURFACE:
      graw_decode_create_surface(ctx, handle);
      break;
   case VIRGL_OBJECT_SAMPLER_VIEW:
      graw_decode_create_sampler_view(ctx, handle);
      break;
   case VIRGL_OBJECT_SAMPLER_STATE:
      graw_decode_create_sampler_state(ctx, handle, length);
      break;
   case VIRGL_OBJECT_QUERY:
      graw_decode_create_query(ctx, handle);
      break;
   case VIRGL_OBJECT_STREAMOUT_TARGET:
      graw_decode_create_stream_output_target(ctx, handle);
      break;
   }
}

static void graw_decode_bind_object(struct grend_decode_ctx *ctx)
{
   uint32_t header = ctx->ds->buf[ctx->ds->buf_offset];
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset+1];
   uint16_t length;
   uint8_t obj_type = (header >> 8) & 0xff;

   length = header >> 16;

   switch (obj_type) {
   case VIRGL_OBJECT_BLEND:
      grend_object_bind_blend(ctx->grctx, handle);
      break;
   case VIRGL_OBJECT_DSA:
      grend_object_bind_dsa(ctx->grctx, handle);
      break;
   case VIRGL_OBJECT_RASTERIZER:
      grend_object_bind_rasterizer(ctx->grctx, handle);
      break;
   case VIRGL_OBJECT_VS:
      grend_bind_vs(ctx->grctx, handle);
      break;
   case VIRGL_OBJECT_FS:
      grend_bind_fs(ctx->grctx, handle);
      break;
   case VIRGL_OBJECT_VERTEX_ELEMENTS:
      grend_bind_vertex_elements_state(ctx->grctx, handle);
      break;
   }
}

static void graw_decode_destroy_object(struct grend_decode_ctx *ctx)
{
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset+1];
   graw_renderer_object_destroy(ctx->grctx, handle);
}

void graw_reset_decode(void)
{
   // free(gdctx->grctx);
   // gdctx->grctx = NULL;
   //gdctx->ds = NULL;
}

static void graw_decode_set_stencil_ref(struct grend_decode_ctx *ctx)
{
   struct pipe_stencil_ref ref;
   uint32_t val = ctx->ds->buf[ctx->ds->buf_offset + 1];
   ref.ref_value[0] = val & 0xff;
   ref.ref_value[1] = (val >> 8) & 0xff;
   grend_set_stencil_ref(ctx->grctx, &ref);
}

static void graw_decode_set_blend_color(struct grend_decode_ctx *ctx)
{
   struct pipe_blend_color color;
   int i;
   
   for (i = 0; i < 4; i++)
      color.color[i] = uif(ctx->ds->buf[ctx->ds->buf_offset + 1 + i]);

   grend_set_blend_color(ctx->grctx, &color);
}

static void graw_decode_set_scissor_state(struct grend_decode_ctx *ctx)
{
   struct pipe_scissor_state ss;
   uint32_t temp;
   
   temp = ctx->ds->buf[ctx->ds->buf_offset + 1];
   ss.minx = temp & 0xffff;
   ss.miny = (temp >> 16) & 0xffff;

   temp = ctx->ds->buf[ctx->ds->buf_offset + 2];
   ss.maxx = temp & 0xffff;
   ss.maxy = (temp >> 16) & 0xffff;

   grend_set_scissor_state(ctx->grctx, &ss);
}

static void graw_decode_set_polygon_stipple(struct grend_decode_ctx *ctx)
{
   struct pipe_poly_stipple ps;
   int i;

   for (i = 0; i < 32; i++)
      ps.stipple[i] = ctx->ds->buf[ctx->ds->buf_offset + 1 + i];

   grend_set_polygon_stipple(ctx->grctx, &ps);
}

static void graw_decode_set_clip_state(struct grend_decode_ctx *ctx)
{
   struct pipe_clip_state clip;
   int i, j;

   for (i = 0; i < 8; i++)
      for (j = 0; j < 4; j++)
         clip.ucp[i][j] = uif(ctx->ds->buf[ctx->ds->buf_offset + 1 + (i * 4) + j]);
   grend_set_clip_state(ctx->grctx, &clip);
}

static void graw_decode_set_sample_mask(struct grend_decode_ctx *ctx)
{
   unsigned mask;

   mask = ctx->ds->buf[ctx->ds->buf_offset + 1];
   grend_set_sample_mask(ctx->grctx, mask);
}

static void graw_decode_resource_copy_region(struct grend_decode_ctx *ctx)
{
   struct pipe_box box;
   uint32_t dst_handle, src_handle;
   uint32_t dst_level, dstx, dsty, dstz;
   uint32_t src_level;

   dst_handle = ctx->ds->buf[ctx->ds->buf_offset + 1];
   dst_level = ctx->ds->buf[ctx->ds->buf_offset + 2];
   dstx = ctx->ds->buf[ctx->ds->buf_offset + 3];
   dsty = ctx->ds->buf[ctx->ds->buf_offset + 4];
   dstz = ctx->ds->buf[ctx->ds->buf_offset + 5];
   src_handle = ctx->ds->buf[ctx->ds->buf_offset + 6];
   src_level = ctx->ds->buf[ctx->ds->buf_offset + 7];
   box.x = ctx->ds->buf[ctx->ds->buf_offset + 8];
   box.y = ctx->ds->buf[ctx->ds->buf_offset + 9];
   box.z = ctx->ds->buf[ctx->ds->buf_offset + 10];
   box.width = ctx->ds->buf[ctx->ds->buf_offset + 11];
   box.height = ctx->ds->buf[ctx->ds->buf_offset + 12];
   box.depth = ctx->ds->buf[ctx->ds->buf_offset + 13];

   graw_renderer_resource_copy_region(ctx->grctx, dst_handle,
                                      dst_level, dstx, dsty, dstz,
                                      src_handle, src_level,
                                      &box);
}


static void graw_decode_blit(struct grend_decode_ctx *ctx)
{
   struct pipe_blit_info info;
   uint32_t dst_handle, src_handle, temp;

   info.mask = ctx->ds->buf[ctx->ds->buf_offset + 1];
   info.filter = ctx->ds->buf[ctx->ds->buf_offset + 2];
   info.scissor_enable = ctx->ds->buf[ctx->ds->buf_offset + 3] & 1;
   temp = ctx->ds->buf[ctx->ds->buf_offset + 4];
   info.scissor.minx = temp & 0xffff;
   info.scissor.miny = (temp >> 16) & 0xffff;
   temp = ctx->ds->buf[ctx->ds->buf_offset + 5];
   info.scissor.maxx = temp & 0xffff;
   info.scissor.maxy = (temp >> 16) & 0xffff;
   dst_handle = ctx->ds->buf[ctx->ds->buf_offset + 6];
   info.dst.level = ctx->ds->buf[ctx->ds->buf_offset + 7];
   info.dst.format = ctx->ds->buf[ctx->ds->buf_offset + 8];
   info.dst.box.x = ctx->ds->buf[ctx->ds->buf_offset + 9];
   info.dst.box.y = ctx->ds->buf[ctx->ds->buf_offset + 10];
   info.dst.box.z = ctx->ds->buf[ctx->ds->buf_offset + 11];
   info.dst.box.width = ctx->ds->buf[ctx->ds->buf_offset + 12];
   info.dst.box.height = ctx->ds->buf[ctx->ds->buf_offset + 13];
   info.dst.box.depth = ctx->ds->buf[ctx->ds->buf_offset + 14];

   src_handle = ctx->ds->buf[ctx->ds->buf_offset + 15];
   info.src.level = ctx->ds->buf[ctx->ds->buf_offset + 16];
   info.src.format = ctx->ds->buf[ctx->ds->buf_offset + 17];
   info.src.box.x = ctx->ds->buf[ctx->ds->buf_offset + 18];
   info.src.box.y = ctx->ds->buf[ctx->ds->buf_offset + 19];
   info.src.box.z = ctx->ds->buf[ctx->ds->buf_offset + 20];
   info.src.box.width = ctx->ds->buf[ctx->ds->buf_offset + 21];
   info.src.box.height = ctx->ds->buf[ctx->ds->buf_offset + 22];
   info.src.box.depth = ctx->ds->buf[ctx->ds->buf_offset + 23]; 

   graw_renderer_blit(ctx->grctx, dst_handle, src_handle, &info);
}

static void graw_decode_bind_sampler_states(struct grend_decode_ctx *ctx, int length)
{
   uint32_t shader_type = ctx->ds->buf[ctx->ds->buf_offset + 1];
   uint32_t start_slot = ctx->ds->buf[ctx->ds->buf_offset + 2];
   uint32_t num_states = length - 1;

   grend_bind_sampler_states(ctx->grctx, shader_type, start_slot, num_states,
                                    &ctx->ds->buf[ctx->ds->buf_offset + 3]);
}

static void graw_decode_begin_query(struct grend_decode_ctx *ctx)
{
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 1];

   grend_begin_query(ctx->grctx, handle);
}

static void graw_decode_end_query(struct grend_decode_ctx *ctx)
{
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 1];

   grend_end_query(ctx->grctx, handle);
}

static void graw_decode_get_query_result(struct grend_decode_ctx *ctx)
{
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 1];
   uint32_t wait = ctx->ds->buf[ctx->ds->buf_offset + 2];
   grend_get_query_result(ctx->grctx, handle, wait);
}

static void graw_decode_set_render_condition(struct grend_decode_ctx *ctx)
{
   uint32_t handle = ctx->ds->buf[ctx->ds->buf_offset + 1];
   boolean condition = ctx->ds->buf[ctx->ds->buf_offset + 2] & 1;
   uint mode = ctx->ds->buf[ctx->ds->buf_offset + 3];
   grend_render_condition(ctx->grctx, handle, condition, mode);
}

static void graw_decode_set_streamout_targets(struct grend_decode_ctx *ctx,
                                              uint16_t length)
{
   uint32_t handles[16];
   uint32_t num_handles = length - 1;
   uint32_t append_bitmask;
   int i;

   append_bitmask = ctx->ds->buf[ctx->ds->buf_offset + 1];
   for (i = 0; i < num_handles; i++)
      handles[i] = ctx->ds->buf[ctx->ds->buf_offset + 2 + i];
   grend_set_streamout_targets(ctx->grctx, append_bitmask, num_handles, handles);
}

static void graw_decode_set_query_state(struct grend_decode_ctx *ctx)
{
   boolean enabled;

   enabled = ctx->ds->buf[ctx->ds->buf_offset + 1] & 0x1;
   grend_set_query_state(ctx->grctx, enabled);
}

void graw_renderer_context_create_internal(uint32_t handle, uint32_t nlen,
                                           const char *debug_name)
{
   struct grend_decode_ctx *dctx;

   if (handle > GRAW_MAX_CTX)
      return;
   
   dctx = malloc(sizeof(struct grend_decode_ctx));
   if (!dctx)
       return;
   
   dctx->grctx = grend_create_context(handle, nlen, debug_name);
   if (!dctx->grctx) {
      free(dctx);
      return;
   }

   dctx->ds = &dctx->ids;

   dec_ctx[handle] = dctx;
}

void graw_renderer_context_create(uint32_t handle, uint32_t nlen, const char *debug_name)
{
   if (handle > GRAW_MAX_CTX)
      return;
   /* context 0 is always available with no guarantees */
   if (handle == 0)
      return;

   graw_renderer_context_create_internal(handle, nlen, debug_name);
}

void graw_renderer_context_destroy(uint32_t handle)
{
   struct grend_decode_ctx *ctx;
   bool ret;
   if (handle > GRAW_MAX_CTX)
      return;

   ctx = dec_ctx[handle];
   dec_ctx[handle] = NULL;
   ret = grend_destroy_context(ctx->grctx);
   free(ctx);
   /* switch to ctx 0 */
   if (ret)
       grend_hw_switch_context(dec_ctx[0]->grctx, TRUE);
}

struct grend_context *vrend_lookup_renderer_ctx(uint32_t ctx_id)
{
   if (ctx_id > GRAW_MAX_CTX)
      return NULL;

   if (dec_ctx[ctx_id] == NULL)
      return NULL;

   return dec_ctx[ctx_id]->grctx;
}

static void graw_decode_block(uint32_t ctx_id, uint32_t *block, int ndw)
{
   int i = 0;
   struct grend_decode_ctx *gdctx;
   boolean ret;
   if (ctx_id > GRAW_MAX_CTX)
      return;

   if (dec_ctx[ctx_id] == NULL)
      return;

   gdctx = dec_ctx[ctx_id];

   ret = grend_hw_switch_context(gdctx->grctx, TRUE);
   if (ret == FALSE)
      return;

   gdctx->ds->buf = block;
   gdctx->ds->buf_total = ndw;
   gdctx->ds->buf_offset = 0;

   while (gdctx->ds->buf_offset < gdctx->ds->buf_total) {
      uint32_t header = gdctx->ds->buf[gdctx->ds->buf_offset];

//      fprintf(stderr,"[%d] cmd is %d (obj %d) len %d\n", gdctx->ds->buf_offset, header & 0xff, (header >> 8 & 0xff), (header >> 16));
      
      switch (header & 0xff) {
      case VIRGL_CCMD_CREATE_OBJECT:
         graw_decode_create_object(gdctx);
         break;
      case VIRGL_CCMD_BIND_OBJECT:
         graw_decode_bind_object(gdctx);
         break;
      case VIRGL_CCMD_DESTROY_OBJECT:
         graw_decode_destroy_object(gdctx);
         break;
      case VIRGL_CCMD_CLEAR:
         graw_decode_clear(gdctx);
         break;
      case VIRGL_CCMD_DRAW_VBO:
         graw_decode_draw_vbo(gdctx);
         break;
      case VIRGL_CCMD_SET_FRAMEBUFFER_STATE:
         graw_decode_set_framebuffer_state(gdctx);
         break;
      case VIRGL_CCMD_SET_VERTEX_BUFFERS:
         graw_decode_set_vertex_buffers(gdctx, header >> 16);
         break;
      case VIRGL_CCMD_RESOURCE_INLINE_WRITE:
         graw_decode_resource_inline_write(gdctx, header >> 16);
         break;
      case VIRGL_CCMD_SET_VIEWPORT_STATE:
         graw_decode_set_viewport_state(gdctx);
         break;
      case VIRGL_CCMD_SET_SAMPLER_VIEWS:
         graw_decode_set_sampler_views(gdctx, header >> 16);
         break;
      case VIRGL_CCMD_SET_INDEX_BUFFER:
         graw_decode_set_index_buffer(gdctx);
         break;
      case VIRGL_CCMD_SET_CONSTANT_BUFFER:
         graw_decode_set_constant_buffer(gdctx, header >> 16);
         break;
      case VIRGL_CCMD_SET_STENCIL_REF:
         graw_decode_set_stencil_ref(gdctx);
         break;
      case VIRGL_CCMD_SET_BLEND_COLOR:
         graw_decode_set_blend_color(gdctx);
         break;
      case VIRGL_CCMD_SET_SCISSOR_STATE:
         graw_decode_set_scissor_state(gdctx);
         break;
      case VIRGL_CCMD_BLIT:
         graw_decode_blit(gdctx);
         break;
      case VIRGL_CCMD_RESOURCE_COPY_REGION:
         graw_decode_resource_copy_region(gdctx);
         break;
      case VIRGL_CCMD_BIND_SAMPLER_STATES:
         graw_decode_bind_sampler_states(gdctx, header >> 16);
         break;
      case VIRGL_CCMD_BEGIN_QUERY:
         graw_decode_begin_query(gdctx);
         break;
      case VIRGL_CCMD_END_QUERY:
         graw_decode_end_query(gdctx);
         break;
      case VIRGL_CCMD_GET_QUERY_RESULT:
         graw_decode_get_query_result(gdctx);
         break;
      case VIRGL_CCMD_SET_POLYGON_STIPPLE:
         graw_decode_set_polygon_stipple(gdctx);
         break;
      case VIRGL_CCMD_SET_CLIP_STATE:
         graw_decode_set_clip_state(gdctx);
         break;
      case VIRGL_CCMD_SET_SAMPLE_MASK:
         graw_decode_set_sample_mask(gdctx);
         break;
      case VIRGL_CCMD_SET_STREAMOUT_TARGETS:
         graw_decode_set_streamout_targets(gdctx, header >> 16);
         break;
      case VIRGL_CCMD_SET_QUERY_STATE:
         graw_decode_set_query_state(gdctx);
         break;
      case VIRGL_CCMD_SET_RENDER_CONDITION:
	 graw_decode_set_render_condition(gdctx);
	 break;
      }
      gdctx->ds->buf_offset += (header >> 16) + 1;
      
   }

}

void graw_decode_block_iov(struct virgl_iovec *iov, unsigned int niovs,
                           uint32_t ctx_id, uint64_t offset, int ndw)
{
   uint32_t *block = (uint32_t *)(iov[0].iov_base + offset);
   void *data;
   if (niovs > 1) {
      data = malloc(ndw * 4);
      graw_iov_to_buf(iov, niovs, offset, data, ndw * 4);
   }
   else
      data = (uint32_t *)(iov[0].iov_base + offset);
   graw_decode_block(ctx_id, data, ndw);
   if (niovs > 1)
      free(data);

}

