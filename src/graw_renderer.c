#include <epoxy/gl.h>

#include <stdio.h>
#include "pipe/p_shader_tokens.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_transfer.h"
#include "util/u_double_list.h"
#include "util/u_format.h"
#include "tgsi/tgsi_text.h"
#include "tgsi/tgsi_info.h"
#include "tgsi/tgsi_parse.h"

#include "vrend_object.h"
#include "graw_shader.h"

#include "graw_renderer.h"
#include "graw_decode.h"
#include "graw_cursor.h"

#include "virgl_hw.h"
/* transfer boxes from the guest POV are in y = 0 = top orientation */
/* blit/copy operations from the guest POV are in y = 0 = top orientation */

/* since we are storing things in OpenGL FBOs we need to flip transfer operations by default */
static void grend_update_viewport_state(struct grend_context *ctx);
static void grend_update_scissor_state(struct grend_context *ctx);
static void grend_ctx_restart_queries(struct grend_context *ctx);
static void grend_destroy_query_object(void *obj_ptr);
static void grend_finish_context_switch(struct grend_context *ctx);
static void grend_patch_blend_func(struct grend_context *ctx);
static void grend_update_frontface_state(struct grend_context *ctx);

extern int graw_shader_use_explicit;
int localrender;
static int have_invert_mesa = 0;
static int draw_cursor = 0;
int vrend_dump_shaders;

static struct grend_if_cbs *clicbs;

struct grend_fence {
   uint32_t fence_id;
   uint32_t ctx_id;
   GLsync syncobj;
   struct list_head fences;
};

struct grend_nontimer_hw_query {
   struct list_head query_list;
   GLuint id;
   uint64_t result;
};

struct grend_query {
   struct list_head waiting_queries;
   struct list_head ctx_queries;

   struct list_head hw_queries;
   GLuint timer_query_id;
   GLuint type;
   GLuint gltype;
   int ctx_id;
   struct grend_resource *res;
   uint64_t current_total;
   boolean active_hw;
};

#define VIRGL_INVALID_RESOURCE 1
struct global_error_state {
   enum virgl_errors last_error;
};

struct global_renderer_state {
   bool viewport_dirty;
   bool scissor_dirty;
   GLboolean blend_enabled;
   GLboolean depth_test_enabled;
   GLboolean alpha_test_enabled;
   GLboolean stencil_test_enabled;
   GLuint program_id;
   struct list_head fence_list;
   struct grend_context *current_ctx;
   struct grend_context *current_hw_ctx;
   struct list_head waiting_query_list;

   struct graw_cursor_info cursor_info;
   boolean have_robustness;
   boolean have_multisample;
   GLuint vaoid;

   struct pipe_rasterizer_state hw_rs_state;
   struct pipe_depth_stencil_alpha_state hw_dsa_state;
   struct pipe_blend_state hw_blend_state;

   boolean have_nv_prim_restart, have_gl_prim_restart, have_bit_encoding;
};

static struct global_renderer_state grend_state;

struct grend_linked_shader_program {
  struct list_head head;
  GLuint id;

  struct grend_shader *ss[PIPE_SHADER_TYPES];

  uint32_t samplers_used_mask[PIPE_SHADER_TYPES];
  GLuint *samp_locs[PIPE_SHADER_TYPES];

  GLuint *shadow_samp_mask_locs[PIPE_SHADER_TYPES];
  GLuint *shadow_samp_add_locs[PIPE_SHADER_TYPES];

  GLuint *const_locs[PIPE_SHADER_TYPES];

  GLuint *attrib_locs;
  uint32_t shadow_samp_mask[PIPE_SHADER_TYPES];

  GLuint vs_ws_adjust_loc;
};

struct grend_shader {
   struct grend_shader *next_variant;
   struct grend_shader_selector *sel;

   GLchar *glsl_prog;
   GLuint id;
   GLuint compiled_fs_id;
   struct vrend_shader_key key;
};

struct grend_shader_selector {
   struct pipe_reference reference;
   struct grend_shader *current;
   struct tgsi_token *tokens;

   struct vrend_shader_info sinfo;

   unsigned num_shaders;
   unsigned type;
};

struct grend_buffer {
   struct grend_resource base;
};

struct grend_texture {
   struct grend_resource base;
   struct pipe_sampler_state state;
   GLenum cur_swizzle_r;
   GLenum cur_swizzle_g;
   GLenum cur_swizzle_b;
   GLenum cur_swizzle_a;
   GLuint srgb_decode;
};

struct grend_surface {
   struct pipe_reference reference;
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   struct grend_resource *texture;
};

struct grend_sampler {

};

struct grend_so_target {
   struct pipe_reference reference;
   GLuint res_handle;
   unsigned buffer_offset;
   unsigned buffer_size;
   struct grend_resource *buffer;
};

struct grend_sampler_view {
   struct pipe_reference reference;
   GLuint id;
   GLuint res_handle;
   GLuint format;
   GLuint val0, val1;
   GLuint swizzle_r:3;
   GLuint swizzle_g:3;
   GLuint swizzle_b:3;
   GLuint swizzle_a:3;
   GLuint gl_swizzle_r;
   GLuint gl_swizzle_g;
   GLuint gl_swizzle_b;
   GLuint gl_swizzle_a;
   GLuint cur_base, cur_max;
   struct grend_resource *texture;
   GLenum depth_texture_mode;
   GLuint srgb_decode;
};

struct grend_vertex_element {
   struct pipe_vertex_element base;
   GLenum type;
   GLboolean norm;
   GLuint nr_chan;
};

struct grend_vertex_element_array {
   unsigned count;
   struct grend_vertex_element elements[PIPE_MAX_ATTRIBS];
};

struct grend_constants {
   float *consts;
   uint32_t num_consts;
};

struct grend_shader_view {
   int num_views;
   struct grend_sampler_view *views[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t res_id[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   uint32_t old_ids[PIPE_MAX_SHADER_SAMPLER_VIEWS];
};

struct grend_context {
   char debug_name[64];

   virgl_gl_context gl_context;

   int ctx_id;
   GLuint vaoid;

   uint32_t enabled_attribs_bitmask;

   struct util_hash_table *object_hash;
   struct grend_vertex_element_array *ve;
   int num_vbos;
   struct pipe_vertex_buffer vbo[PIPE_MAX_ATTRIBS];
   uint32_t vbo_res_ids[PIPE_MAX_ATTRIBS];
   struct grend_shader_selector *vs;
   struct grend_shader_selector *fs;

   bool shader_dirty;
   struct grend_linked_shader_program *prog;

   struct grend_shader_view views[PIPE_SHADER_TYPES];

   struct pipe_index_buffer ib;
   uint32_t index_buffer_res_id;

   struct grend_constants consts[PIPE_SHADER_TYPES];
   bool const_dirty[PIPE_SHADER_TYPES];
   struct pipe_sampler_state *sampler_state[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];

   int num_sampler_states[PIPE_SHADER_TYPES];
   boolean sampler_state_dirty;

   uint8_t stencil_refs[2];

   struct pipe_depth_stencil_alpha_state *dsa;
   boolean stencil_state_dirty;
   struct list_head programs;

   GLint view_cur_x, view_cur_y;
   GLsizei view_width, view_height;
   GLclampd view_near_val, view_far_val;
   float depth_transform, depth_scale;
   /* viewport is negative */
   GLboolean viewport_is_negative;
   /* this is set if the contents of the FBO look upside down when viewed
      with 0,0 as the bottom corner */
   GLboolean inverted_fbo_content;
   boolean scissor_state_dirty;
   boolean viewport_state_dirty;
   uint32_t fb_height;

   uint32_t fb_id;
   int nr_cbufs, old_nr_cbufs;
   struct grend_surface *zsurf;
   struct grend_surface *surf[8];
   
   struct pipe_scissor_state ss;

   struct pipe_blend_state blend_state;
   struct pipe_depth_stencil_alpha_state dsa_state;
   struct pipe_rasterizer_state rs_state;

   struct pipe_blend_color blend_color;

   int num_so_targets;
   struct grend_so_target *so_targets[16];

   struct list_head active_nontimer_query_list;
   boolean query_on_hw;

   /* has this ctx gotten an error? */
   boolean in_error;
   enum virgl_ctx_errors last_error;

   boolean ctx_switch_pending;
   GLuint blit_fb_ids[2];
};

static struct grend_nontimer_hw_query *grend_create_hw_query(struct grend_query *query);

#define MAX_SCANOUT 4
static struct grend_resource *frontbuffer[MAX_SCANOUT];
static struct pipe_box front_box[MAX_SCANOUT];
static struct grend_format_table tex_conv_table[VIRGL_FORMAT_MAX];

static INLINE boolean vrend_format_can_render(enum virgl_formats format)
{
   return tex_conv_table[format].bindings & VREND_BIND_RENDER;
}

static INLINE boolean vrend_format_is_ds(enum virgl_formats format)
{
   return tex_conv_table[format].bindings & VREND_BIND_DEPTHSTENCIL;
}

static const char *vrend_ctx_error_strings[] = { "None", "Unknown", "Illegal shader", "Illegal handle", "Illegal resource", "Illegal surface", "Illegal vertex format" };

static void __report_context_error(const char *fname, struct grend_context *ctx, enum virgl_ctx_errors error, uint32_t value)
{
   ctx->in_error = TRUE;
   ctx->last_error = error;
   fprintf(stderr,"%s: context error reported %d \"%s\" %s %d\n", fname, ctx->ctx_id, ctx->debug_name, vrend_ctx_error_strings[error], value);
}
#define report_context_error(ctx, error, value) __report_context_error(__func__, ctx, error, value)

static INLINE boolean should_invert_viewport(struct grend_context *ctx)
{
   /* if we have a negative viewport then gallium wanted to invert it,
      however since we are rendering to GL FBOs we need to invert it
      again unless we are rendering upside down already
      - confused? 
      so if gallium asks for a negative viewport */
   return !(ctx->viewport_is_negative ^ ctx->inverted_fbo_content);
}

static void grend_destroy_surface(struct grend_surface *surf)
{
   grend_resource_reference(&surf->texture, NULL);
   free(surf);
}

static INLINE void
grend_surface_reference(struct grend_surface **ptr, struct grend_surface *surf)
{
   struct grend_surface *old_surf = *ptr;

   if (pipe_reference(&(*ptr)->reference, &surf->reference))
      grend_destroy_surface(old_surf);
   *ptr = surf;
}

static void grend_destroy_sampler_view(struct grend_sampler_view *samp)
{
   grend_resource_reference(&samp->texture, NULL);
   free(samp);
}

static INLINE void
grend_sampler_view_reference(struct grend_sampler_view **ptr, struct grend_sampler_view *view)
{
   struct grend_sampler_view *old_view = *ptr;

   if (pipe_reference(&(*ptr)->reference, &view->reference))
      grend_destroy_sampler_view(old_view);
   *ptr = view;
}

static void grend_destroy_so_target(struct grend_so_target *target)
{
   grend_resource_reference(&target->buffer, NULL);
   free(target);
}

static INLINE void
grend_so_target_reference(struct grend_so_target **ptr, struct grend_so_target *target)
{
   struct grend_so_target *old_target = *ptr;

   if (pipe_reference(&(*ptr)->reference, &target->reference))
      grend_destroy_so_target(old_target);
   *ptr = target;
}

static void grend_shader_destroy(struct grend_shader *shader)
{
   glDeleteShader(shader->id);
   free(shader->glsl_prog);
   free(shader);
}

static void grend_destroy_shader_selector(struct grend_shader_selector *sel)
{
   struct grend_shader *p = sel->current, *c;

   while (p) {
      c = p->next_variant;
      grend_shader_destroy(p);
      p = c;
   }
   free(sel->sinfo.interpinfo);
   free(sel->tokens);
   free(sel);
}

static boolean grend_compile_shader(struct grend_context *ctx,
                                struct grend_shader *shader)
{
   GLint param;
   glShaderSource(shader->id, 1, (const char **)&shader->glsl_prog, NULL);
   glCompileShader(shader->id);
   glGetShaderiv(shader->id, GL_COMPILE_STATUS, &param);
   if (param == GL_FALSE) {
      char infolog[65536];
      int len;
      glGetShaderInfoLog(shader->id, 65536, &len, infolog);
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SHADER, 0);
      fprintf(stderr,"shader failed to compile\n%s\n", infolog);
      fprintf(stderr,"GLSL:\n%s\n", shader->glsl_prog);
      return FALSE;
   }
   return TRUE;
}

static INLINE void
grend_shader_state_reference(struct grend_shader_selector **ptr, struct grend_shader_selector *shader)
{
   struct grend_shader_selector *old_shader = *ptr;

   if (pipe_reference(&(*ptr)->reference, &shader->reference))
      grend_destroy_shader_selector(old_shader);
   *ptr = shader;
}

void
grend_insert_format(struct grend_format_table *entry, uint32_t bindings)
{
   tex_conv_table[entry->format] = *entry;
   tex_conv_table[entry->format].bindings = bindings;
}

static boolean grend_is_timer_query(GLenum gltype)
{
	return gltype == GL_TIMESTAMP ||
               gltype == GL_TIME_ELAPSED;
}

void grend_use_program(GLuint program_id)
{
   if (grend_state.program_id != program_id) {
      glUseProgram(program_id);
      grend_state.program_id = program_id;
   }
}

void grend_bind_va(GLuint vaoid)
{
   glBindVertexArray(vaoid);
}

void grend_blend_enable(GLboolean blend_enable)
{
   if (grend_state.blend_enabled != blend_enable) {
      grend_state.blend_enabled = blend_enable;
      if (blend_enable)
         glEnable(GL_BLEND);
      else
         glDisable(GL_BLEND);
   }
}

void grend_depth_test_enable(GLboolean depth_test_enable)
{
   if (grend_state.depth_test_enabled != depth_test_enable) {
      grend_state.depth_test_enabled = depth_test_enable;
      if (depth_test_enable)
         glEnable(GL_DEPTH_TEST);
      else
         glDisable(GL_DEPTH_TEST);
   }
}

static void grend_alpha_test_enable(GLboolean alpha_test_enable)
{
   if (grend_state.alpha_test_enabled != alpha_test_enable) {
      grend_state.alpha_test_enabled = alpha_test_enable;
      if (alpha_test_enable)
         glEnable(GL_ALPHA_TEST);
      else
         glDisable(GL_ALPHA_TEST);
   }
}
static void grend_stencil_test_enable(GLboolean stencil_test_enable)
{
   if (grend_state.stencil_test_enabled != stencil_test_enable) {
      grend_state.stencil_test_enabled = stencil_test_enable;
      if (stencil_test_enable)
         glEnable(GL_STENCIL_TEST);
      else
         glDisable(GL_STENCIL_TEST);
   }
}

static void set_stream_out_varyings(int prog_id, struct pipe_stream_output_info *vs_so)
{
   char *varyings[PIPE_MAX_SHADER_OUTPUTS];
   char tmp[64];
   int i;
   if (!vs_so->num_outputs)
      return;

   for (i = 0; i < vs_so->num_outputs; i++) {
      snprintf(tmp, 64, "tfout%d", i);

      varyings[i] = strdup(tmp);
   }

   glTransformFeedbackVaryings(prog_id, vs_so->num_outputs,
                               (const GLchar **)varyings, GL_INTERLEAVED_ATTRIBS_EXT);

   for (i = 0; i < vs_so->num_outputs; i++)
      if (varyings[i])
         free(varyings[i]);
}

static struct grend_linked_shader_program *add_shader_program(struct grend_context *ctx,
                                                              struct grend_shader *vs,
                                                              struct grend_shader *fs)
{
  struct grend_linked_shader_program *sprog = malloc(sizeof(struct grend_linked_shader_program));
  char name[16];
  int i;
  GLuint prog_id;
  GLint lret;
  int id;

  /* need to rewrite VS code to add interpolation params */
  if (!vs->compiled_fs_id != fs->id) {
     vrend_patch_vertex_shader_interpolants(vs->glsl_prog,
                                            &vs->sel->sinfo,
                                            &fs->sel->sinfo);
     boolean ret;
     ret = grend_compile_shader(ctx, vs);
     if (ret == FALSE) {
        glDeleteShader(vs->id);
        free(sprog);
        return NULL;
     }
     vs->compiled_fs_id = fs->id;
  }

  prog_id = glCreateProgram();
  glAttachShader(prog_id, vs->id);
  set_stream_out_varyings(prog_id, &vs->sel->sinfo.so_info);
  glAttachShader(prog_id, fs->id);
  glLinkProgram(prog_id);

  glGetProgramiv(prog_id, GL_LINK_STATUS, &lret);
  if (lret == GL_FALSE) {
     char infolog[65536];
     int len;
     glGetProgramInfoLog(prog_id, 65536, &len, infolog);
     fprintf(stderr,"got error linking\n%s\n", infolog);
     /* dump shaders */
     report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SHADER, 0);
     fprintf(stderr,"vert shader: GLSL\n%s\n", vs->glsl_prog);
     fprintf(stderr,"frag shader: GLSL\n%s\n", fs->glsl_prog);
     glDeleteProgram(prog_id);
     return NULL;
  }

  sprog->ss[0] = vs;
  sprog->ss[1] = fs;

  sprog->id = prog_id;

  list_add(&sprog->head, &ctx->programs);

  sprog->vs_ws_adjust_loc = glGetUniformLocation(prog_id, "winsys_adjust");
  for (id = PIPE_SHADER_VERTEX; id <= PIPE_SHADER_FRAGMENT; id++) {
    if (sprog->ss[id]->sel->sinfo.samplers_used_mask) {
       uint32_t mask = sprog->ss[id]->sel->sinfo.samplers_used_mask;
       int nsamp = util_bitcount(sprog->ss[id]->sel->sinfo.samplers_used_mask);
       int index;
       sprog->shadow_samp_mask[id] = sprog->ss[id]->sel->sinfo.shadow_samp_mask;
       if (sprog->ss[id]->sel->sinfo.shadow_samp_mask) {
          sprog->shadow_samp_mask_locs[id] = calloc(nsamp, sizeof(uint32_t));
          sprog->shadow_samp_add_locs[id] = calloc(nsamp, sizeof(uint32_t));
       } else {
          sprog->shadow_samp_mask_locs[id] = sprog->shadow_samp_add_locs[id] = NULL;
       }
       sprog->samp_locs[id] = calloc(nsamp, sizeof(uint32_t));
       if (sprog->samp_locs[id]) {
          const char *prefix = (id == PIPE_SHADER_VERTEX) ? "vs" : "fs";
          index = 0;
          while(mask) {
             i = u_bit_scan(&mask);
             snprintf(name, 10, "%ssamp%d", prefix, i);
             sprog->samp_locs[id][index] = glGetUniformLocation(prog_id, name);
             if (sprog->ss[id]->sel->sinfo.shadow_samp_mask & (1 << i)) {
                snprintf(name, 14, "%sshadmask%d", prefix, i);
                sprog->shadow_samp_mask_locs[id][index] = glGetUniformLocation(prog_id, name);
                snprintf(name, 14, "%sshadadd%d", prefix, i);
                sprog->shadow_samp_add_locs[id][index] = glGetUniformLocation(prog_id, name);
             }
             index++;
          }
       }
    } else {
       sprog->samp_locs[id] = NULL;
       sprog->shadow_samp_mask_locs[id] = NULL;
       sprog->shadow_samp_add_locs[id] = NULL;
       sprog->shadow_samp_mask[id] = 0;
    }
    sprog->samplers_used_mask[id] = sprog->ss[id]->sel->sinfo.samplers_used_mask;
  }
  
  for (id = PIPE_SHADER_VERTEX; id <= PIPE_SHADER_FRAGMENT; id++) {
     if (sprog->ss[id]->sel->sinfo.num_consts) {
        sprog->const_locs[id] = calloc(sprog->ss[id]->sel->sinfo.num_consts, sizeof(uint32_t));
        if (sprog->const_locs[id]) {
           const char *prefix = (id == PIPE_SHADER_VERTEX) ? "vs" : "fs";
           for (i = 0; i < sprog->ss[id]->sel->sinfo.num_consts; i++) {
              snprintf(name, 16, "%sconst[%d]", prefix, i);
              sprog->const_locs[id][i] = glGetUniformLocation(prog_id, name);
           }
        }
     } else
        sprog->const_locs[id] = NULL;
  }
  
  if (vs->sel->sinfo.num_inputs) {
    sprog->attrib_locs = calloc(vs->sel->sinfo.num_inputs, sizeof(uint32_t));
    if (sprog->attrib_locs) {
      for (i = 0; i < vs->sel->sinfo.num_inputs; i++) {
	snprintf(name, 10, "in_%d", i);
	sprog->attrib_locs[i] = glGetAttribLocation(prog_id, name);
      }
    }
  } else
    sprog->attrib_locs = NULL;
   
  if (fs->sel->sinfo.num_outputs > 1) {
     glBindFragDataLocationIndexed(prog_id, 0, 0, "out_c0");
     glBindFragDataLocationIndexed(prog_id, 0, 1, "out_c1");
  }
  return sprog;
}

static struct grend_linked_shader_program *lookup_shader_program(struct grend_context *ctx,
                                                                 GLuint vs_id, GLuint fs_id)
{
  struct grend_linked_shader_program *ent;
  LIST_FOR_EACH_ENTRY(ent, &ctx->programs, head) {
     if (ent->ss[PIPE_SHADER_VERTEX]->id == vs_id && ent->ss[PIPE_SHADER_FRAGMENT]->id == fs_id)
        return ent;
  }
  return 0;
}

static void grend_free_programs(struct grend_context *ctx)
{
   struct grend_linked_shader_program *ent, *tmp;
   int i;
   LIST_FOR_EACH_ENTRY_SAFE(ent, tmp, &ctx->programs, head) {
      glDeleteProgram(ent->id);
      list_del(&ent->head);

      for (i = PIPE_SHADER_VERTEX; i <= PIPE_SHADER_FRAGMENT; i++) {
//         grend_shader_state_reference(&ent->ss[i], NULL);
         free(ent->shadow_samp_mask_locs[i]);
         free(ent->shadow_samp_add_locs[i]);
         free(ent->samp_locs[i]);
         free(ent->const_locs[i]);
      }
      free(ent->attrib_locs);
      free(ent);
   }
}  

static void grend_apply_sampler_state(struct grend_context *ctx,
                                      struct grend_resource *res,
                                      uint32_t shader_type,
                                      int id);

void grend_update_stencil_state(struct grend_context *ctx);

void grend_create_surface(struct grend_context *ctx,
                          uint32_t handle,
                          uint32_t res_handle, uint32_t format,
                          uint32_t val0, uint32_t val1)
   
{
   struct grend_surface *surf;
   struct grend_resource *res;

   res = vrend_resource_lookup(res_handle, ctx->ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   surf = CALLOC_STRUCT(grend_surface);
   surf->res_handle = res_handle;
   surf->format = format;
   surf->val0 = val0;
   surf->val1 = val1;
   pipe_reference_init(&surf->reference, 1);

   grend_resource_reference(&surf->texture, res);

   vrend_object_insert(ctx->object_hash, surf, sizeof(*surf), handle, VIRGL_OBJECT_SURFACE);
}

static void grend_destroy_surface_object(void *obj_ptr)
{
   struct grend_surface *surface = obj_ptr;

   grend_surface_reference(&surface, NULL);
}

static void grend_destroy_sampler_view_object(void *obj_ptr)
{
   struct grend_sampler_view *samp = obj_ptr;

   grend_sampler_view_reference(&samp, NULL);
}

static void grend_destroy_so_target_object(void *obj_ptr)
{
   struct grend_so_target *target = obj_ptr;

   grend_so_target_reference(&target, NULL);
}

static inline GLenum to_gl_swizzle(int swizzle)
{
   switch (swizzle) {
   case PIPE_SWIZZLE_RED: return GL_RED;
   case PIPE_SWIZZLE_GREEN: return GL_GREEN;
   case PIPE_SWIZZLE_BLUE: return GL_BLUE;
   case PIPE_SWIZZLE_ALPHA: return GL_ALPHA;
   case PIPE_SWIZZLE_ZERO: return GL_ZERO;
   case PIPE_SWIZZLE_ONE: return GL_ONE;
   }
   assert(0);
   return 0;
}
void grend_create_sampler_view(struct grend_context *ctx,
                               uint32_t handle,
                               uint32_t res_handle, uint32_t format,
                               uint32_t val0, uint32_t val1, uint32_t swizzle_packed)
{
   struct grend_sampler_view *view;
   struct grend_resource *res;

   res = vrend_resource_lookup(res_handle, ctx->ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }
   
   view = CALLOC_STRUCT(grend_sampler_view);
   pipe_reference_init(&view->reference, 1);
   view->res_handle = res_handle;
   view->format = format;
   view->val0 = val0;
   view->val1 = val1;
   view->swizzle_r = swizzle_packed & 0x7;
   view->swizzle_g = (swizzle_packed >> 3) & 0x7;
   view->swizzle_b = (swizzle_packed >> 6) & 0x7;
   view->swizzle_a = (swizzle_packed >> 9) & 0x7;
   view->cur_base = -1;
   view->cur_max = 10000;

   grend_resource_reference(&view->texture, res);

   if (view->swizzle_r != 0 && view->swizzle_g != 1 && view->swizzle_b != 2 && view->swizzle_a != 3)
      fprintf(stderr,"%d %d swizzles %d %d %d %d\n", view->format, view->texture->base.format, view->swizzle_r, view->swizzle_g, view->swizzle_b, view->swizzle_a);      

   view->srgb_decode = GL_DECODE_EXT;
   if (view->format != view->texture->base.format) {
      if (util_format_is_srgb(view->texture->base.format) &&
          !util_format_is_srgb(view->format))
         view->srgb_decode = GL_SKIP_DECODE_EXT;
   }
   if (util_format_has_alpha(format) || util_format_is_depth_or_stencil(format))
      view->gl_swizzle_a = to_gl_swizzle(view->swizzle_a);
   else
      view->gl_swizzle_a = GL_ONE;
   view->gl_swizzle_r = to_gl_swizzle(view->swizzle_r);
   view->gl_swizzle_g = to_gl_swizzle(view->swizzle_g);
   view->gl_swizzle_b = to_gl_swizzle(view->swizzle_b);

   vrend_object_insert(ctx->object_hash, view, sizeof(*view), handle, VIRGL_OBJECT_SAMPLER_VIEW);
}

static void grend_fb_bind_texture(struct grend_resource *res,
                                  int idx,
                                  uint32_t level, uint32_t layer)
{
    const struct util_format_description *desc = util_format_description(res->base.format);
    GLenum attachment = GL_COLOR_ATTACHMENT0_EXT + idx;

    if (vrend_format_is_ds(res->base.format)) { {
            if (util_format_has_stencil(desc)) {
                if (util_format_has_depth(desc))
                    attachment = GL_DEPTH_STENCIL_ATTACHMENT;
                else
                    attachment = GL_STENCIL_ATTACHMENT;
            } else
                attachment = GL_DEPTH_ATTACHMENT;
        }
    }

    switch (res->target) {
    case GL_TEXTURE_1D_ARRAY:
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        glFramebufferTextureLayer(GL_FRAMEBUFFER_EXT, attachment,
                                  res->id, level, layer);
        break;
    case GL_TEXTURE_3D:
        glFramebufferTexture3DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                  res->target, res->id, level, layer);
        break;
    case GL_TEXTURE_CUBE_MAP:
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                  GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, res->id, level);
        break;
    case GL_TEXTURE_1D:
        glFramebufferTexture1DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                  res->target, res->id, level);
        break;
    case GL_TEXTURE_2D:
    default:
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                  res->target, res->id, level);
        break;
    }

    if (attachment == GL_DEPTH_ATTACHMENT)
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT,
                                  0, 0, 0);
}

static void grend_hw_set_zsurf_texture(struct grend_context *ctx)
{
   struct grend_resource *tex;

   if (!ctx->zsurf) {
      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_TEXTURE_2D, 0, 0);
      return;

   }
   tex = ctx->zsurf->texture;
   if (!tex)
      return;

   grend_fb_bind_texture(tex, 0, ctx->zsurf->val0, ctx->zsurf->val1 & 0xffff);
}

static void grend_hw_set_color_surface(struct grend_context *ctx, int index)
{
   struct grend_resource *tex;

   if (!ctx->surf[index]) {
      GLenum attachment = GL_COLOR_ATTACHMENT0 + index;

      glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, attachment,
                                GL_TEXTURE_2D, 0, 0);
   } else {
       tex = ctx->surf[index]->texture;
       grend_fb_bind_texture(tex, index, ctx->surf[index]->val0,
                             ctx->surf[index]->val1 & 0xffff);

   }


}

static void grend_hw_emit_framebuffer_state(struct grend_context *ctx)
{
   static const GLenum buffers[8] = {
      GL_COLOR_ATTACHMENT0_EXT,
      GL_COLOR_ATTACHMENT1_EXT,
      GL_COLOR_ATTACHMENT2_EXT,
      GL_COLOR_ATTACHMENT3_EXT,
      GL_COLOR_ATTACHMENT4_EXT,
      GL_COLOR_ATTACHMENT5_EXT,
      GL_COLOR_ATTACHMENT6_EXT,
      GL_COLOR_ATTACHMENT7_EXT,
   };
   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   if (ctx->nr_cbufs == 0)
       glReadBuffer(GL_NONE);
   glDrawBuffers(ctx->nr_cbufs, buffers);
}

void grend_set_framebuffer_state(struct grend_context *ctx,
                                 uint32_t nr_cbufs, uint32_t surf_handle[8],
                                 uint32_t zsurf_handle)
{
   struct grend_surface *surf, *zsurf;
   int i;
   int old_num;
   GLenum status;
   GLint new_height = -1;
   boolean new_ibf = GL_FALSE;

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   if (zsurf_handle) {
      zsurf = vrend_object_lookup(ctx->object_hash, zsurf_handle, VIRGL_OBJECT_SURFACE);
      if (!zsurf) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SURFACE, zsurf_handle);
         return;
      }
   } else
      zsurf = NULL;

   if (ctx->zsurf != zsurf) {
      grend_surface_reference(&ctx->zsurf, zsurf);
      grend_hw_set_zsurf_texture(ctx);
   }

   old_num = ctx->nr_cbufs;
   ctx->nr_cbufs = nr_cbufs;
   ctx->old_nr_cbufs = old_num;

   for (i = 0; i < nr_cbufs; i++) {
      surf = vrend_object_lookup(ctx->object_hash, surf_handle[i], VIRGL_OBJECT_SURFACE);
      if (!surf) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_SURFACE, surf_handle[i]);
         return;
      }
      if (ctx->surf[i] != surf) {
         grend_surface_reference(&ctx->surf[i], surf);
         grend_hw_set_color_surface(ctx, i);
      }
   }

   if (old_num > ctx->nr_cbufs) {
      for (i = ctx->nr_cbufs; i < old_num; i++) {
         grend_surface_reference(&ctx->surf[i], NULL);
         grend_hw_set_color_surface(ctx, i);
      }
   }

   /* find a buffer to set fb_height from */
   if (ctx->nr_cbufs == 0 && !ctx->zsurf) {
       new_height = 0;
       new_ibf = FALSE;
   } else if (ctx->nr_cbufs == 0) {
       new_height = u_minify(ctx->zsurf->texture->base.height0, ctx->zsurf->val0);
       new_ibf = ctx->zsurf->texture->y_0_top ? TRUE : FALSE;
   } 
   else {
       new_height = u_minify(ctx->surf[0]->texture->base.height0, ctx->surf[0]->val0);
       new_ibf = ctx->surf[0]->texture->y_0_top ? TRUE : FALSE;
   }

   if (new_height != -1) {
      if (ctx->fb_height != new_height || ctx->inverted_fbo_content != new_ibf) {
         ctx->fb_height = new_height;
         ctx->inverted_fbo_content = new_ibf;
         ctx->scissor_state_dirty = TRUE;
         ctx->viewport_state_dirty = TRUE;
      }
   }

   grend_hw_emit_framebuffer_state(ctx);

   if (ctx->nr_cbufs > 0 || ctx->zsurf) {
      status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE)
         fprintf(stderr,"failed to complete framebuffer 0x%x %s\n", status, ctx->debug_name);
   }
}

static void grend_hw_emit_depth_range(struct grend_context *ctx)
{
   glDepthRange(ctx->view_near_val, ctx->view_far_val);
}

/*
 * if the viewport Y scale factor is > 0 then we are rendering to
 * an FBO already so don't need to invert rendering?
 */
void grend_set_viewport_state(struct grend_context *ctx,
                              const struct pipe_viewport_state *state)
{
   /* convert back to glViewport */
   GLint x, y;
   GLsizei width, height;
   GLclampd near_val, far_val;
   GLboolean viewport_is_negative = (state->scale[1] < 0) ? GL_TRUE : GL_FALSE;
   GLfloat abs_s1 = fabsf(state->scale[1]);

   width = state->scale[0] * 2.0f;
   height = abs_s1 * 2.0f;
   x = state->translate[0] - state->scale[0];
   y = state->translate[1] - state->scale[1];

   near_val = state->translate[2] - state->scale[2];
   far_val = near_val + (state->scale[2] * 2.0);

   if (ctx->view_cur_x != x ||
       ctx->view_cur_y != y ||
       ctx->view_width != width ||
       ctx->view_height != height) {
      ctx->viewport_state_dirty = TRUE;
      ctx->view_cur_x = x;
      ctx->view_cur_y = y;
      ctx->view_width = width;
      ctx->view_height = height;
   }

   if (ctx->viewport_is_negative != viewport_is_negative)
      ctx->viewport_is_negative = viewport_is_negative;

   ctx->depth_scale = fabsf(far_val - near_val);
   ctx->depth_transform = near_val;

   if (ctx->view_near_val != near_val ||
       ctx->view_far_val != far_val) {
      ctx->view_near_val = near_val;
      ctx->view_far_val = far_val;
      grend_hw_emit_depth_range(ctx);
   }
}

void grend_create_vertex_elements_state(struct grend_context *ctx,
                                        uint32_t handle,
                                        unsigned num_elements,
                                        const struct pipe_vertex_element *elements)
{
   struct grend_vertex_element_array *v = CALLOC_STRUCT(grend_vertex_element_array);
   const struct util_format_description *desc;
   GLenum type;
   int i;

   v->count = num_elements;
   for (i = 0; i < num_elements; i++) {
      memcpy(&v->elements[i].base, &elements[i], sizeof(struct pipe_vertex_element));

      desc = util_format_description(elements[i].src_format);
      
      type = GL_FALSE;
      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT) {
	 if (desc->channel[0].size == 32)
	    type = GL_FLOAT;
	 else if (desc->channel[0].size == 64)
	    type = GL_DOUBLE;
         else if (desc->channel[0].size == 16)
            type = GL_HALF_FLOAT;
      } else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
                 desc->channel[0].size == 8) 
         type = GL_UNSIGNED_BYTE;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 8) 
         type = GL_BYTE;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 16) 
         type = GL_UNSIGNED_SHORT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 16) 
         type = GL_SHORT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_UNSIGNED &&
               desc->channel[0].size == 32) 
         type = GL_UNSIGNED_INT;
      else if (desc->channel[0].type == UTIL_FORMAT_TYPE_SIGNED &&
               desc->channel[0].size == 32) 
         type = GL_INT;
      else if (elements[i].src_format == PIPE_FORMAT_R10G10B10A2_SSCALED ||
               elements[i].src_format == PIPE_FORMAT_R10G10B10A2_SNORM ||
               elements[i].src_format == PIPE_FORMAT_B10G10R10A2_SNORM)
         type = GL_INT_2_10_10_10_REV;
      else if (elements[i].src_format == PIPE_FORMAT_R10G10B10A2_USCALED ||
               elements[i].src_format == PIPE_FORMAT_R10G10B10A2_UNORM ||
               elements[i].src_format == PIPE_FORMAT_B10G10R10A2_UNORM)
         type = GL_UNSIGNED_INT_2_10_10_10_REV;

      if (type == GL_FALSE) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_VERTEX_FORMAT, elements[i].src_format);
         FREE(v);
         return;
      }

      v->elements[i].type = type;
      if (desc->channel[0].normalized)
         v->elements[i].norm = GL_TRUE;
      if (desc->nr_channels == 4 && desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_Z)
         v->elements[i].nr_chan = GL_BGRA;
      else
         v->elements[i].nr_chan = desc->nr_channels;
   }

   vrend_object_insert(ctx->object_hash, v, sizeof(struct grend_vertex_element), handle,
                      VIRGL_OBJECT_VERTEX_ELEMENTS);
}

void grend_bind_vertex_elements_state(struct grend_context *ctx,
                                      uint32_t handle)
{
   struct grend_vertex_element_array *v;

   if (!handle) {
      ctx->ve = NULL;
      return;
   }
   v = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_VERTEX_ELEMENTS);
   if (!v) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }
   ctx->ve = v;
}

void grend_set_constants(struct grend_context *ctx,
                         uint32_t shader,
                         uint32_t index,
                         uint32_t num_constant,
                         float *data)
{
   struct grend_constants *consts;
   int i;

   consts = &ctx->consts[shader];
   ctx->const_dirty[shader] = TRUE;

   consts->consts = realloc(consts->consts, num_constant * sizeof(float));
   if (!consts->consts)
      return;

   consts->num_consts = num_constant;
   for (i = 0; i < num_constant; i++)
      consts->consts[i] = data[i];
}

void grend_set_index_buffer(struct grend_context *ctx,
                            uint32_t res_handle,
                            uint32_t index_size,
                            uint32_t offset)
{
   struct grend_resource *res;

   ctx->ib.index_size = index_size;
   ctx->ib.offset = offset;
   if (res_handle) {
      if (ctx->index_buffer_res_id != res_handle) {
         res = vrend_resource_lookup(res_handle, ctx->ctx_id);
         if (!res) {
            grend_resource_reference((struct grend_resource **)&ctx->ib.buffer, NULL);
            ctx->index_buffer_res_id = 0;
            report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
            return;
         }
         grend_resource_reference((struct grend_resource **)&ctx->ib.buffer, res);
         ctx->index_buffer_res_id = res_handle;
      }
   } else {
      grend_resource_reference((struct grend_resource **)&ctx->ib.buffer, NULL);
      ctx->index_buffer_res_id = 0;
   }
}

void grend_set_single_vbo(struct grend_context *ctx,
                         int index,
                         uint32_t stride,
                         uint32_t buffer_offset,
                         uint32_t res_handle)
{
   struct grend_resource *res;
   ctx->vbo[index].stride = stride;
   ctx->vbo[index].buffer_offset = buffer_offset;

   if (res_handle == 0) {
      grend_resource_reference((struct grend_resource **)&ctx->vbo[index].buffer, NULL);
      ctx->vbo_res_ids[index] = 0;
   } else if (ctx->vbo_res_ids[index] != res_handle) {
      res = vrend_resource_lookup(res_handle, ctx->ctx_id);
      if (!res) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
         ctx->vbo_res_ids[index] = 0;
         return;
      }
      grend_resource_reference((struct grend_resource **)&ctx->vbo[index].buffer, res);
      ctx->vbo_res_ids[index] = res_handle;
   }
}

void grend_set_num_vbo(struct grend_context *ctx,
                      int num_vbo)
{                                              
   int old_num = ctx->num_vbos;
   int i;
   ctx->num_vbos = num_vbo;

   for (i = num_vbo; i < old_num; i++) {
      grend_resource_reference((struct grend_resource **)&ctx->vbo[i].buffer, NULL);
      ctx->vbo_res_ids[i] = 0;
   }

}

void grend_set_single_sampler_view(struct grend_context *ctx,
                                   uint32_t shader_type,
                                   int index,
                                   uint32_t handle)
{
   struct grend_sampler_view *view = NULL;
   struct grend_texture *tex;

   if (handle) {
      view = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_SAMPLER_VIEW);
      if (!view) {
         ctx->views[shader_type].views[index] = NULL;
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
         return;
      }
      tex = vrend_resource_lookup(view->res_handle, ctx->ctx_id);
      if (!tex) {
         fprintf(stderr,"cannot find texture to back resource view %d %d\n", handle, view->res_handle);
         return;
      }
      glBindTexture(view->texture->target, view->texture->id);
      if (view->texture->target != PIPE_BUFFER) {
         tex = (struct grend_texture *)view->texture;
         if (util_format_is_depth_or_stencil(view->format)) {
            if (view->depth_texture_mode != GL_RED) {
               glTexParameteri(view->texture->target, GL_DEPTH_TEXTURE_MODE, GL_RED);
               view->depth_texture_mode = GL_RED;
            }
         }

         if (view->cur_base != (view->val1 & 0xff)) {
            view->cur_base = view->val1 & 0xff;
            glTexParameteri(view->texture->target, GL_TEXTURE_BASE_LEVEL, view->cur_base);
         }
         if (view->cur_max != ((view->val1 >> 8) & 0xff)) {
            view->cur_max = (view->val1 >> 8) & 0xff;
            glTexParameteri(view->texture->target, GL_TEXTURE_MAX_LEVEL, view->cur_max);
         }
         if (tex->cur_swizzle_r != view->gl_swizzle_r) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_R, view->gl_swizzle_r);
            tex->cur_swizzle_r = view->gl_swizzle_r;
         }
         if (tex->cur_swizzle_g != view->gl_swizzle_g) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_G, view->gl_swizzle_g);
            tex->cur_swizzle_g = view->gl_swizzle_g;
         }
         if (tex->cur_swizzle_b != view->gl_swizzle_b) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_B, view->gl_swizzle_b);
            tex->cur_swizzle_b = view->gl_swizzle_b;
         }
         if (tex->cur_swizzle_a != view->gl_swizzle_a) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SWIZZLE_A, view->gl_swizzle_a);
            tex->cur_swizzle_a = view->gl_swizzle_a;
         }
         if (tex->srgb_decode != view->srgb_decode && util_format_is_srgb(tex->base.base.format)) {
            glTexParameteri(view->texture->target, GL_TEXTURE_SRGB_DECODE_EXT,
                            view->srgb_decode);
            tex->srgb_decode = view->srgb_decode;
         }
      }
   }

   grend_sampler_view_reference(&ctx->views[shader_type].views[index], view);
}

void grend_set_num_sampler_views(struct grend_context *ctx,
                                    uint32_t shader_type,
                                    uint32_t start_slot,
                                    int num_sampler_views)
{
   if (start_slot + num_sampler_views < ctx->views[shader_type].num_views) {
      int i;
      for (i = start_slot + num_sampler_views; i < ctx->views[shader_type].num_views; i++)
         grend_sampler_view_reference(&ctx->views[shader_type].views[i], NULL);
   }
   ctx->views[shader_type].num_views = start_slot + num_sampler_views;
}

void grend_transfer_inline_write(struct grend_context *ctx,
                                 uint32_t res_handle,
                                 unsigned level,
                                 unsigned usage,
                                 const struct pipe_box *box,
                                 const void *data,
                                 unsigned stride,
                                 unsigned layer_stride)
{
   struct grend_resource *res;

   res = vrend_resource_lookup(res_handle, ctx->ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }
   if (res->ptr) {
      memcpy(res->ptr + box->x, data, box->width);
   } else if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
      glBindBufferARB(res->target, res->id);
      glBufferSubData(res->target, box->x, box->width, data);
   } else {
      GLenum glformat, gltype;
      glBindTexture(res->target, res->id);
      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      glTexSubImage2D(res->target, level, box->x, box->y, box->width, box->height,
                      glformat, gltype, data);
   }
}


static void grend_destroy_shader_object(void *obj_ptr)
{
   struct grend_shader_selector *state = obj_ptr;

   grend_shader_state_reference(&state, NULL);
}

static INLINE void vrend_fill_shader_key(struct grend_context *ctx,
                                         struct vrend_shader_key *key)
{
   key->invert_fs_origin = !ctx->inverted_fbo_content;
   key->coord_replace = ctx->rs_state.point_quad_rasterization ? ctx->rs_state.sprite_coord_enable : 0;
}

static int grend_shader_create(struct grend_context *ctx,
                               struct grend_shader *shader,
                               struct vrend_shader_key key)
{
   shader->id = glCreateShader(shader->sel->type == PIPE_SHADER_VERTEX ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
   shader->compiled_fs_id = 0;
   shader->glsl_prog = tgsi_convert(shader->sel->tokens, &key, &shader->sel->sinfo);

   if (shader->sel->type == PIPE_SHADER_FRAGMENT) {
      boolean ret;

      ret = grend_compile_shader(ctx, shader);
      if (ret == FALSE) {
         glDeleteShader(shader->id);
         free(shader->glsl_prog);
         return -1;
      }
   }
   return 0;
}

static int grend_shader_select(struct grend_context *ctx,
                               struct grend_shader_selector *sel,
                               boolean *dirty)
{
   struct vrend_shader_key key;
   struct grend_shader *shader = NULL;
   int r;

   memset(&key, 0, sizeof(key));
   vrend_fill_shader_key(ctx, &key);

   if (sel->current && memcmp(&sel->current->key, &key, sizeof(key)))
      return 0;

   if (sel->num_shaders > 1) {
      struct grend_shader *p = sel->current, *c = p->next_variant;
      while (c && memcmp(&c->key, &key, sizeof(key)) != 0) {
         p = c;
         c = c->next_variant;
      }
      if (c) {
         p->next_variant = c->next_variant;
         shader = c;
      }
   }

   if (!shader) {
      shader = CALLOC_STRUCT(grend_shader);
      shader->sel = sel;

      r = grend_shader_create(ctx, shader, key);
      if (r) {
         sel->current = NULL;
         FREE(shader);
         return r;
      }
      sel->num_shaders++;
   }
   if (dirty)
      *dirty = true;

   shader->next_variant = sel->current;
   sel->current = shader;
   return 0;
}

static void *grend_create_shader_state(struct grend_context *ctx,
                                       const struct pipe_shader_state *state,
                                       unsigned pipe_shader_type)
{
   struct grend_shader_selector *sel = CALLOC_STRUCT(grend_shader_selector);
   int r;

   sel->type = pipe_shader_type;
   sel->sinfo.so_info = state->stream_output;
   sel->tokens = tgsi_dup_tokens(state->tokens);
   pipe_reference_init(&sel->reference, 1);

   r = grend_shader_select(ctx, sel, NULL);
   if (r)
      return NULL;
   return sel;
}

void grend_create_vs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *vs)
{
   struct grend_shader_selector *sel;

   sel = grend_create_shader_state(ctx, vs, PIPE_SHADER_VERTEX);

   vrend_object_insert(ctx->object_hash, sel, sizeof(*sel), handle, VIRGL_OBJECT_VS);

   return;
}

void grend_create_fs(struct grend_context *ctx,
                     uint32_t handle,
                     const struct pipe_shader_state *fs)
{
   struct grend_shader_selector *sel;

   sel = grend_create_shader_state(ctx, fs, PIPE_SHADER_FRAGMENT);

   vrend_object_insert(ctx->object_hash, sel, sizeof(*sel), handle, VIRGL_OBJECT_FS);

   return;
}

void grend_bind_vs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_selector *sel;

   sel = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_VS);

   if (ctx->vs != sel)
      ctx->shader_dirty = true;
   grend_shader_state_reference(&ctx->vs, sel);
}

void grend_bind_fs(struct grend_context *ctx,
                   uint32_t handle)
{
   struct grend_shader_selector *sel;

   sel = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_FS);

   if (ctx->fs != sel)
      ctx->shader_dirty = true;
   grend_shader_state_reference(&ctx->fs, sel);
}

void grend_clear(struct grend_context *ctx,
                 unsigned buffers,
                 const union pipe_color_union *color,
                 double depth, unsigned stencil)
{
   GLbitfield bits = 0;

   if (ctx->in_error)
      return;

   if (ctx->ctx_switch_pending)
      grend_finish_context_switch(ctx);

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);

   grend_update_frontface_state(ctx);
   if (ctx->stencil_state_dirty)
      grend_update_stencil_state(ctx);
   if (ctx->scissor_state_dirty || grend_state.scissor_dirty)
      grend_update_scissor_state(ctx);
   if (ctx->viewport_state_dirty || grend_state.viewport_dirty)
      grend_update_viewport_state(ctx);

   grend_use_program(0);

   if (buffers & PIPE_CLEAR_COLOR)
      glClearColor(color->f[0], color->f[1], color->f[2], color->f[3]);

   if (buffers & PIPE_CLEAR_DEPTH) {
      /* gallium clears don't respect depth mask */
      glDepthMask(GL_TRUE);
      glClearDepth(depth);
   }

   if (buffers & PIPE_CLEAR_STENCIL)
      glClearStencil(stencil);

   if (buffers & PIPE_CLEAR_COLOR)
      bits |= GL_COLOR_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_DEPTH)
      bits |= GL_DEPTH_BUFFER_BIT;
   if (buffers & PIPE_CLEAR_STENCIL)
      bits |= GL_STENCIL_BUFFER_BIT;
   glClear(bits);

   if (buffers & PIPE_CLEAR_DEPTH)
      if (!ctx->dsa_state.depth.writemask)
         glDepthMask(GL_FALSE);
}

static void grend_update_scissor_state(struct grend_context *ctx)
{
   struct pipe_scissor_state *ss = &ctx->ss;
   struct pipe_rasterizer_state *state = &ctx->rs_state;
   GLint y;

   if (ctx->viewport_is_negative)
      y = ss->miny;
   else
      y = ss->maxy;
   if (state->scissor)
      glEnable(GL_SCISSOR_TEST);
   else
      glDisable(GL_SCISSOR_TEST);

   glScissor(ss->minx, y, ss->maxx - ss->minx, ss->maxy - ss->miny);
   ctx->scissor_state_dirty = FALSE;
   grend_state.scissor_dirty = FALSE;
}

static void grend_update_viewport_state(struct grend_context *ctx)
{
   GLint cy;
   if (ctx->viewport_is_negative)
      cy = ctx->view_cur_y - ctx->view_height;
   else
      cy = ctx->view_cur_y;
   glViewport(ctx->view_cur_x, cy, ctx->view_width, ctx->view_height);

   ctx->viewport_state_dirty = FALSE;
   grend_state.viewport_dirty = FALSE;
}

static GLenum get_xfb_mode(GLenum mode)
{
   switch (mode) {
   case GL_POINTS:
      return GL_POINTS;
   case GL_TRIANGLES:
   case GL_TRIANGLE_STRIP:
   case GL_TRIANGLE_FAN:
   case GL_QUADS:
   case GL_QUAD_STRIP:
   case GL_POLYGON:
      return GL_TRIANGLES;
   case GL_LINES:
   case GL_LINE_LOOP:
   case GL_LINE_STRIP:
      return GL_LINES;
   }
   fprintf(stderr, "failed to translate TFB %d\n", mode);
   return GL_POINTS;
}

void grend_draw_vbo(struct grend_context *ctx,
                    const struct pipe_draw_info *info)
{
   int i;
   int sampler_id;
   bool new_program = FALSE;
   uint32_t shader_type;
   uint32_t num_enable;
   uint32_t enable_bitmask;
   uint32_t disable_bitmask;

   if (ctx->in_error)
      return;

   if (ctx->ctx_switch_pending)
      grend_finish_context_switch(ctx);

   grend_update_frontface_state(ctx);
   if (ctx->stencil_state_dirty)
      grend_update_stencil_state(ctx);
   if (ctx->scissor_state_dirty || grend_state.scissor_dirty)
      grend_update_scissor_state(ctx);

   if (ctx->viewport_state_dirty || grend_state.viewport_dirty)
      grend_update_viewport_state(ctx);

   grend_patch_blend_func(ctx);

   if (ctx->shader_dirty) {
     struct grend_linked_shader_program *prog;
     boolean fs_dirty, vs_dirty;

     if (!ctx->vs || !ctx->fs) {
        fprintf(stderr,"dropping rendering due to missing shaders\n");
        return;
     }

     grend_shader_select(ctx, ctx->fs, &fs_dirty);
     grend_shader_select(ctx, ctx->vs, &vs_dirty);

     prog = lookup_shader_program(ctx, ctx->vs->current->id, ctx->fs->current->id);
     if (!prog) {
        prog = add_shader_program(ctx, ctx->vs->current, ctx->fs->current);
        if (!prog)
           return;
     }
     if (ctx->prog != prog) {
       new_program = TRUE;
       ctx->prog = prog;
     }
   }

   glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, ctx->fb_id);
   
   grend_use_program(ctx->prog->id);

   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= PIPE_SHADER_FRAGMENT; shader_type++) {
      if (ctx->prog->const_locs[shader_type] && (ctx->const_dirty[shader_type] || new_program)) {
	 int nc;
         if (shader_type == PIPE_SHADER_VERTEX) {
	    nc = ctx->vs->sinfo.num_consts;
         } else if (shader_type == PIPE_SHADER_FRAGMENT) {
	    nc = ctx->fs->sinfo.num_consts;
         }
         for (i = 0; i < nc; i++) {
            if (ctx->prog->const_locs[shader_type][i] != -1 && ctx->consts[shader_type].consts)
               glUniform4fv(ctx->prog->const_locs[shader_type][i], 1, &ctx->consts[shader_type].consts[i * 4]);
         }
         ctx->const_dirty[shader_type] = FALSE;
      }
   }

   sampler_id = 0;
   for (shader_type = PIPE_SHADER_VERTEX; shader_type <= PIPE_SHADER_FRAGMENT; shader_type++) {
      int index = 0;
      for (i = 0; i < ctx->views[shader_type].num_views; i++) {
         struct grend_resource *texture = NULL;

         if (ctx->views[shader_type].views[i]) {
            texture = ctx->views[shader_type].views[i]->texture;
         }

         if (!(ctx->prog->samplers_used_mask[shader_type] & (1 << i)))
             continue;

         if (ctx->prog->samp_locs[shader_type])
            glUniform1i(ctx->prog->samp_locs[shader_type][index], sampler_id);

         if (ctx->prog->shadow_samp_mask[shader_type] & (1 << i)) {
            struct grend_sampler_view *tview = ctx->views[shader_type].views[i];
            glUniform4f(ctx->prog->shadow_samp_mask_locs[shader_type][index], 
                        tview->gl_swizzle_r == GL_ZERO ? 0.0 : 1.0, 
                        tview->gl_swizzle_g == GL_ZERO ? 0.0 : 1.0, 
                        tview->gl_swizzle_b == GL_ZERO ? 0.0 : 1.0, 
                        tview->gl_swizzle_a == GL_ZERO ? 0.0 : 1.0);
            glUniform4f(ctx->prog->shadow_samp_add_locs[shader_type][index], 
                        tview->gl_swizzle_r == GL_ONE ? 1.0 : 0.0, 
                        tview->gl_swizzle_g == GL_ONE ? 1.0 : 0.0, 
                        tview->gl_swizzle_b == GL_ONE ? 1.0 : 0.0, 
                        tview->gl_swizzle_a == GL_ONE ? 1.0 : 0.0);
         }
            
         glActiveTexture(GL_TEXTURE0 + sampler_id);
         if (texture) {
            glBindTexture(texture->target, texture->id);
            if (ctx->views[shader_type].old_ids[i] != texture->id || ctx->sampler_state_dirty) {
               grend_apply_sampler_state(ctx, texture, shader_type, i);
               ctx->views[shader_type].old_ids[i] = texture->id;
            }
            if (ctx->rs_state.point_quad_rasterization) {
               if (ctx->rs_state.sprite_coord_enable & (1 << i))
                  glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE);
               else
                  glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_FALSE);
            }
            sampler_id++;
         }
         index++;
      }
   } 
   ctx->sampler_state_dirty = FALSE;

   if (!ctx->ve) {
      fprintf(stderr,"illegal VE setup - skipping renderering\n");
      return;
   }
   glUniform4f(ctx->prog->vs_ws_adjust_loc, 0.0, ctx->viewport_is_negative ? -1.0 : 1.0, ctx->depth_scale, ctx->depth_transform);

   num_enable = ctx->ve->count;
   enable_bitmask = 0;
   disable_bitmask = ~((1ull << num_enable) - 1);
   for (i = 0; i < ctx->ve->count; i++) {
      struct grend_vertex_element *ve = &ctx->ve->elements[i];
      int vbo_index = ctx->ve->elements[i].base.vertex_buffer_index;
      struct grend_buffer *buf;
      GLint loc;

      if (i >= ctx->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs) {
         /* XYZZY: debug this? */
         num_enable = ctx->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs;
         break;
      }
      buf = (struct grend_buffer *)ctx->vbo[vbo_index].buffer;

      if (!buf) {
           fprintf(stderr,"cannot find vbo buf %d %d %d\n", i, ctx->ve->count, ctx->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs);
           continue;
      }

      if (graw_shader_use_explicit) {
         loc = i;
      } else {
	if (ctx->prog->attrib_locs) {
	  loc = ctx->prog->attrib_locs[i];
	} else loc = -1;

	if (loc == -1) {
           fprintf(stderr,"cannot find loc %d %d %d\n", i, ctx->ve->count, ctx->prog->ss[PIPE_SHADER_VERTEX]->sel->sinfo.num_inputs);
          num_enable--;
          if (i == 0) {
             fprintf(stderr,"shader probably didn't compile - skipping rendering\n");
             return;
          }
          continue;
        }
      }

      if (ve->type == GL_FALSE) {
	fprintf(stderr,"failed to translate vertex type - skipping render\n");
	return;
      }

      glBindBuffer(GL_ARRAY_BUFFER, buf->base.id);

      if (ctx->vbo[vbo_index].stride == 0) {
         void *data;
         /* for 0 stride we are kinda screwed */
         data = glMapBufferRange(GL_ARRAY_BUFFER, ctx->vbo[vbo_index].buffer_offset, ve->nr_chan * sizeof(GLfloat), GL_MAP_READ_BIT);
         
         switch (ve->nr_chan) {
         case 1:
            glVertexAttrib1fv(loc, data);
            break;
         case 2:
            glVertexAttrib2fv(loc, data);
            break;
         case 3:
            glVertexAttrib3fv(loc, data);
            break;
         case 4:
         default:
            glVertexAttrib4fv(loc, data);
            break;
         }
         glUnmapBuffer(GL_ARRAY_BUFFER);
         disable_bitmask |= (1 << loc);
      } else {
         enable_bitmask |= (1 << loc);
         if (util_format_is_pure_integer(ve->base.src_format)) {
            glVertexAttribIPointer(loc, ve->nr_chan, ve->type, ctx->vbo[vbo_index].stride, (void *)(unsigned long)(ve->base.src_offset + ctx->vbo[vbo_index].buffer_offset));
         } else {
            glVertexAttribPointer(loc, ve->nr_chan, ve->type, ve->norm, ctx->vbo[vbo_index].stride, (void *)(unsigned long)(ve->base.src_offset + ctx->vbo[vbo_index].buffer_offset));
         }
         glVertexAttribDivisorARB(loc, ve->base.instance_divisor);
      }
   }

   if (ctx->enabled_attribs_bitmask != enable_bitmask) {
      uint32_t mask = ctx->enabled_attribs_bitmask & disable_bitmask;

      while (mask) {
         i = u_bit_scan(&mask);
         glDisableVertexAttribArray(i);
      }
      ctx->enabled_attribs_bitmask &= ~disable_bitmask;

      mask = ctx->enabled_attribs_bitmask ^ enable_bitmask;
      while (mask) {
         i = u_bit_scan(&mask);
         glEnableVertexAttribArray(i);
      }

      ctx->enabled_attribs_bitmask = enable_bitmask;
   }

   if (info->indexed) {
      struct grend_resource *res = (struct grend_resource *)ctx->ib.buffer;
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res->id);
   } else
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

//   grend_ctx_restart_queries(ctx);

   if (ctx->num_so_targets) {
      glBeginTransformFeedback(get_xfb_mode(info->mode));
   }

   if (info->primitive_restart) {
      if (grend_state.have_nv_prim_restart) {
         glEnableClientState(GL_PRIMITIVE_RESTART_NV);
         glPrimitiveRestartIndexNV(info->restart_index);
      } else {
         glEnable(GL_PRIMITIVE_RESTART);
         glPrimitiveRestartIndex(info->restart_index);
      }
   }
   /* set the vertex state up now on a delay */
   if (!info->indexed) {
      GLenum mode = info->mode;
      if (info->instance_count <= 1)
         glDrawArrays(mode, info->start, info->count);
      else
         glDrawArraysInstancedARB(mode, info->start, info->count, info->instance_count);
   } else {
      GLenum elsz;
      GLenum mode = info->mode;
      switch (ctx->ib.index_size) {
      case 1:
         elsz = GL_UNSIGNED_BYTE;
         break;
      case 2: 
         elsz = GL_UNSIGNED_SHORT;
         break;
      case 4: 
         elsz = GL_UNSIGNED_INT;
         break;
      }

      if (info->index_bias) {
         if (info->min_index != 0 || info->max_index != -1)
            glDrawRangeElementsBaseVertex(mode, info->min_index, info->max_index, info->count, elsz, (void *)(unsigned long)ctx->ib.offset, info->index_bias);
         else
            glDrawElementsBaseVertex(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset, info->index_bias);
      } else if (info->min_index != 0 || info->max_index != -1)
         glDrawRangeElements(mode, info->min_index, info->max_index, info->count, elsz, (void *)(unsigned long)ctx->ib.offset);                  
      else if (info->instance_count > 1)
         glDrawElementsInstancedARB(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset, info->instance_count);
      else
         glDrawElements(mode, info->count, elsz, (void *)(unsigned long)ctx->ib.offset);
   }

   if (ctx->num_so_targets)
      glEndTransformFeedback();

   if (info->primitive_restart) {
      if (grend_state.have_nv_prim_restart)
         glDisableClientState(GL_PRIMITIVE_RESTART_NV);
      else if (grend_state.have_gl_prim_restart)
         glDisable(GL_PRIMITIVE_RESTART);
   }
}

static GLenum translate_blend_func(uint32_t pipe_blend)
{
   switch(pipe_blend){
   case PIPE_BLEND_ADD: return GL_FUNC_ADD;
   case PIPE_BLEND_SUBTRACT: return GL_FUNC_SUBTRACT;
   case PIPE_BLEND_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
   case PIPE_BLEND_MIN: return GL_MIN;
   case PIPE_BLEND_MAX: return GL_MAX;
   default:
      assert("invalid blend token()" == NULL);
      return 0;
   }
}

static GLenum translate_blend_factor(uint32_t pipe_factor)
{
   switch (pipe_factor) {
   case PIPE_BLENDFACTOR_ONE: return GL_ONE;
   case PIPE_BLENDFACTOR_SRC_COLOR: return GL_SRC_COLOR;
   case PIPE_BLENDFACTOR_SRC_ALPHA: return GL_SRC_ALPHA;

   case PIPE_BLENDFACTOR_DST_COLOR: return GL_DST_COLOR;
   case PIPE_BLENDFACTOR_DST_ALPHA: return GL_DST_ALPHA;

   case PIPE_BLENDFACTOR_CONST_COLOR: return GL_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_CONST_ALPHA: return GL_CONSTANT_ALPHA;

   case PIPE_BLENDFACTOR_SRC1_COLOR: return GL_SRC1_COLOR;
   case PIPE_BLENDFACTOR_SRC1_ALPHA: return GL_SRC1_ALPHA;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
   case PIPE_BLENDFACTOR_ZERO: return GL_ZERO;


   case PIPE_BLENDFACTOR_INV_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;

   case PIPE_BLENDFACTOR_INV_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;

   case PIPE_BLENDFACTOR_INV_CONST_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA: return GL_ONE_MINUS_CONSTANT_ALPHA;

   case PIPE_BLENDFACTOR_INV_SRC1_COLOR: return GL_ONE_MINUS_SRC1_COLOR;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA: return GL_ONE_MINUS_SRC1_ALPHA;

   default:
      assert("invalid blend token()" == NULL);
      return 0;
   }
}

static GLenum
translate_logicop(GLuint pipe_logicop)
{
   switch (pipe_logicop) {
#define CASE(x) case PIPE_LOGICOP_##x: return GL_##x
      CASE(CLEAR);
      CASE(NOR);
      CASE(AND_INVERTED);
      CASE(COPY_INVERTED);
      CASE(AND_REVERSE);
      CASE(INVERT);
      CASE(XOR);
      CASE(NAND);
      CASE(AND);
      CASE(EQUIV);
      CASE(NOOP);
      CASE(OR_INVERTED);
      CASE(COPY);
      CASE(OR_REVERSE);
      CASE(OR);
      CASE(SET);
   default:
      assert("invalid logicop token()" == NULL);
      return 0;
   }
#undef CASE
}

static GLenum
translate_stencil_op(GLuint op)
{
   switch (op) {
#define CASE(x) case PIPE_STENCIL_OP_##x: return GL_##x   
      CASE(KEEP);
      CASE(ZERO);
      CASE(REPLACE);
      CASE(INCR);
      CASE(DECR);
      CASE(INCR_WRAP);
      CASE(DECR_WRAP);
      CASE(INVERT);
   default:
      assert("invalid stencilop token()" == NULL);
      return 0;
   }
#undef CASE
}

static INLINE boolean is_dst_blend(int blend_factor)
{
   return (blend_factor == PIPE_BLENDFACTOR_DST_ALPHA ||
           blend_factor == PIPE_BLENDFACTOR_INV_DST_ALPHA);
}

static INLINE int conv_dst_blend(int blend_factor)
{
   if (blend_factor == PIPE_BLENDFACTOR_DST_ALPHA)
      return PIPE_BLENDFACTOR_ONE;
   if (blend_factor == PIPE_BLENDFACTOR_INV_DST_ALPHA)
      return PIPE_BLENDFACTOR_ZERO;
   return blend_factor;
}

static void grend_patch_blend_func(struct grend_context *ctx)
{
   struct pipe_blend_state *state = &ctx->blend_state;
   int i;
   int rsf, rdf, asf, adf;
   if (ctx->nr_cbufs == 0)
      return;

   for (i = 0; i < ctx->nr_cbufs; i++) {
      if (!util_format_has_alpha(ctx->surf[i]->format))
         break;
   }

   if (i == ctx->nr_cbufs)
      return;

   if (state->independent_blend_enable) {
      /* ARB_draw_buffers_blend is required for this */
      for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
         if (state->rt[i].blend_enable) {
            if (!(is_dst_blend(state->rt[i].rgb_src_factor) ||
                  is_dst_blend(state->rt[i].rgb_dst_factor) ||
                  is_dst_blend(state->rt[i].alpha_src_factor) ||
                  is_dst_blend(state->rt[i].alpha_dst_factor)))
               continue;
            
            rsf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_src_factor));
            rdf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_dst_factor));
            asf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_src_factor));
            adf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_dst_factor));
               
            glBlendFuncSeparateiARB(i, rsf, rdf, asf, adf);
         }
      }
   } else {
      if (state->rt[0].blend_enable) {
            if (!(is_dst_blend(state->rt[0].rgb_src_factor) ||
                  is_dst_blend(state->rt[0].rgb_dst_factor) ||
                  is_dst_blend(state->rt[0].alpha_src_factor) ||
                  is_dst_blend(state->rt[0].alpha_dst_factor)))
               return;

            rsf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_src_factor));
            rdf = translate_blend_factor(conv_dst_blend(state->rt[i].rgb_dst_factor));
            asf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_src_factor));
            adf = translate_blend_factor(conv_dst_blend(state->rt[i].alpha_dst_factor));

            glBlendFuncSeparate(rsf, rdf, asf, adf);
      }
   }
}

static void grend_hw_emit_blend(struct grend_context *ctx)
{
   struct pipe_blend_state *state = &ctx->blend_state;

   if (state->logicop_enable != grend_state.hw_blend_state.logicop_enable) {
      grend_state.hw_blend_state.logicop_enable = state->logicop_enable;
      if (state->logicop_enable) {
         glEnable(GL_COLOR_LOGIC_OP);
         glLogicOp(translate_logicop(state->logicop_func));
      } else
         glDisable(GL_COLOR_LOGIC_OP);
   }

   if (state->independent_blend_enable) {
      /* ARB_draw_buffers_blend is required for this */
      int i;

      for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
         if (state->rt[i].blend_enable) {
            glBlendFuncSeparateiARB(i, translate_blend_factor(state->rt[i].rgb_src_factor),
                                 translate_blend_factor(state->rt[i].rgb_dst_factor),
                                 translate_blend_factor(state->rt[i].alpha_src_factor),
                                 translate_blend_factor(state->rt[i].alpha_dst_factor));
            glBlendEquationSeparateiARB(i, translate_blend_func(state->rt[0].rgb_func),
                                     translate_blend_func(state->rt[0].alpha_func));
            glEnableIndexedEXT(GL_BLEND, i);
         } else
            glDisableIndexedEXT(GL_BLEND, i);

         if (state->rt[i].colormask != grend_state.hw_blend_state.rt[i].colormask) {
            grend_state.hw_blend_state.rt[i].colormask = state->rt[i].colormask;
            glColorMaskIndexedEXT(i, state->rt[i].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                                  state->rt[i].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                                  state->rt[i].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                                  state->rt[i].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
         }
      }
   } else {
      if (state->rt[0].blend_enable) {
         glBlendFuncSeparate(translate_blend_factor(state->rt[0].rgb_src_factor),
                             translate_blend_factor(state->rt[0].rgb_dst_factor),
                             translate_blend_factor(state->rt[0].alpha_src_factor),
                             translate_blend_factor(state->rt[0].alpha_dst_factor));
         glBlendEquationSeparate(translate_blend_func(state->rt[0].rgb_func),
                                 translate_blend_func(state->rt[0].alpha_func));
         grend_blend_enable(GL_TRUE);
      } 
      else
         grend_blend_enable(GL_FALSE);

      if (state->rt[0].colormask != grend_state.hw_blend_state.rt[0].colormask) {
         int i;
         for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++)
            grend_state.hw_blend_state.rt[i].colormask = state->rt[i].colormask;
         glColorMask(state->rt[0].colormask & PIPE_MASK_R ? GL_TRUE : GL_FALSE,
                     state->rt[0].colormask & PIPE_MASK_G ? GL_TRUE : GL_FALSE,
                     state->rt[0].colormask & PIPE_MASK_B ? GL_TRUE : GL_FALSE,
                     state->rt[0].colormask & PIPE_MASK_A ? GL_TRUE : GL_FALSE);
      }
   }

   if (grend_state.have_multisample) {
      if (state->alpha_to_coverage)
         glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
      else
         glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
      
      if (state->alpha_to_one)
         glEnable(GL_SAMPLE_ALPHA_TO_ONE);
      else
         glDisable(GL_SAMPLE_ALPHA_TO_ONE);
   }
}

void grend_object_bind_blend(struct grend_context *ctx,
                             uint32_t handle)
{
   struct pipe_blend_state *state;

   if (handle == 0) {
      memset(&ctx->blend_state, 0, sizeof(ctx->blend_state));
      grend_blend_enable(GL_FALSE);
      return;
   }
   state = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_BLEND);
   if (!state) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }

   ctx->blend_state = *state;

   grend_hw_emit_blend(ctx);
}

static void grend_hw_emit_dsa(struct grend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = &ctx->dsa_state;

   if (state->depth.enabled) {
      grend_depth_test_enable(GL_TRUE);
      glDepthFunc(GL_NEVER + state->depth.func);
      if (state->depth.writemask)
         glDepthMask(GL_TRUE);
      else
         glDepthMask(GL_FALSE);
   } else
      grend_depth_test_enable(GL_FALSE);
 
   if (state->alpha.enabled) {
      grend_alpha_test_enable(GL_TRUE);
      glAlphaFunc(GL_NEVER + state->alpha.func, state->alpha.ref_value);
   } else
      grend_alpha_test_enable(GL_FALSE);


}
void grend_object_bind_dsa(struct grend_context *ctx,
                           uint32_t handle)
{
   struct pipe_depth_stencil_alpha_state *state;

   if (handle == 0) {
      memset(&ctx->dsa_state, 0, sizeof(ctx->dsa_state));
      ctx->dsa = NULL;
      ctx->stencil_state_dirty = TRUE;
      grend_hw_emit_dsa(ctx);
      return;
   }

   state = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_DSA);
   if (!state) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }

   if (ctx->dsa != state)
      ctx->stencil_state_dirty = TRUE;
   ctx->dsa_state = *state;
   ctx->dsa = state;
   grend_hw_emit_dsa(ctx);
}

static void grend_update_frontface_state(struct grend_context *ctx)
{
   struct pipe_rasterizer_state *state = &ctx->rs_state;
   int front_ccw = state->front_ccw;

   front_ccw ^= (ctx->viewport_is_negative ?  1 : 0);
//   if (front_ccw != grend_state.hw_rs_state.front_ccw) {
//      grend_state.hw_rs_state.front_ccw = front_ccw;
      if (front_ccw)
         glFrontFace(GL_CCW);
      else
         glFrontFace(GL_CW);
//   }
}

void grend_update_stencil_state(struct grend_context *ctx)
{
   struct pipe_depth_stencil_alpha_state *state = ctx->dsa;
   int i;
   if (!state)
      return;

   if (!state->stencil[1].enabled) {
      if (state->stencil[0].enabled) {
         grend_stencil_test_enable(GL_TRUE);

         glStencilOp(translate_stencil_op(state->stencil[0].fail_op), 
                     translate_stencil_op(state->stencil[0].zfail_op),
                     translate_stencil_op(state->stencil[0].zpass_op));

         glStencilFunc(GL_NEVER + state->stencil[0].func,
                       ctx->stencil_refs[0],
                       state->stencil[0].valuemask);
         glStencilMask(state->stencil[0].writemask);
      } else
         grend_stencil_test_enable(GL_FALSE);
   } else {
      grend_stencil_test_enable(GL_TRUE);

      for (i = 0; i < 2; i++) {
         GLenum face = (i == 1) ? GL_BACK : GL_FRONT;
         glStencilOpSeparate(face,
                             translate_stencil_op(state->stencil[i].fail_op),
                             translate_stencil_op(state->stencil[i].zfail_op),
                             translate_stencil_op(state->stencil[i].zpass_op));

         glStencilFuncSeparate(face, GL_NEVER + state->stencil[i].func,
                               ctx->stencil_refs[i],
                               state->stencil[i].valuemask);
         glStencilMaskSeparate(face, state->stencil[i].writemask);
      }
   }
   ctx->stencil_state_dirty = FALSE;
}

static inline GLenum translate_fill(uint32_t mode)
{
   switch (mode) {
   case PIPE_POLYGON_MODE_POINT:
      return GL_POINT;
   case PIPE_POLYGON_MODE_LINE:
      return GL_LINE;
   case PIPE_POLYGON_MODE_FILL:
      return GL_FILL;
   }
   assert(0);
   return 0;
}

static void grend_hw_emit_rs(struct grend_context *ctx)
{
   struct pipe_rasterizer_state *state = &ctx->rs_state;
   int i;
#if 0
   if (state->depth_clip) {
      glEnable(GL_DEPTH_CLAMP);
   } else {
      glDisable(GL_DEPTH_CLAMP);
   }
#endif

   if (state->point_size_per_vertex) {
      glEnable(GL_PROGRAM_POINT_SIZE);
   } else {
      glDisable(GL_PROGRAM_POINT_SIZE);
      if (state->point_size)
          glPointSize(state->point_size);
   }

   if (state->rasterizer_discard != grend_state.hw_rs_state.rasterizer_discard) {
      grend_state.hw_rs_state.rasterizer_discard = state->rasterizer_discard;
      if (state->rasterizer_discard)
         glEnable(GL_RASTERIZER_DISCARD);
      else
         glDisable(GL_RASTERIZER_DISCARD);
   }

   glPolygonMode(GL_FRONT, translate_fill(state->fill_front));
   glPolygonMode(GL_BACK, translate_fill(state->fill_back));

   if (state->offset_tri)
      glEnable(GL_POLYGON_OFFSET_FILL);
   else
      glDisable(GL_POLYGON_OFFSET_FILL);

   if (state->offset_line)
      glEnable(GL_POLYGON_OFFSET_LINE);
   else
      glDisable(GL_POLYGON_OFFSET_LINE);

   if (state->offset_point)
      glEnable(GL_POLYGON_OFFSET_POINT);
   else
      glDisable(GL_POLYGON_OFFSET_POINT);
   
   if (state->flatshade != grend_state.hw_rs_state.flatshade) {
      grend_state.hw_rs_state.flatshade = state->flatshade;
      if (state->flatshade) {
         glShadeModel(GL_FLAT);
      } else {
         glShadeModel(GL_SMOOTH);
      }
   }

   if (state->flatshade_first != grend_state.hw_rs_state.flatshade_first) {
      grend_state.hw_rs_state.flatshade_first = state->flatshade_first;
      if (state->flatshade_first)
         glProvokingVertexEXT(GL_FIRST_VERTEX_CONVENTION_EXT);
      else
         glProvokingVertexEXT(GL_LAST_VERTEX_CONVENTION_EXT);
   }
   glPolygonOffset(state->offset_scale, state->offset_units);

   if (state->poly_stipple_enable)
      glEnable(GL_POLYGON_STIPPLE);
   else
      glDisable(GL_POLYGON_STIPPLE);
   
   if (state->point_quad_rasterization) {
      glEnable(GL_POINT_SPRITE);

      glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, state->sprite_coord_mode ? GL_UPPER_LEFT : GL_LOWER_LEFT);
   } else
      glDisable(GL_POINT_SPRITE);

   if (state->cull_face != PIPE_FACE_NONE) {
      switch (state->cull_face) {
      case PIPE_FACE_FRONT:
         glCullFace(GL_FRONT);
         break;
      case PIPE_FACE_BACK:
         glCullFace(GL_BACK);
         break;
      case PIPE_FACE_FRONT_AND_BACK:
         glCullFace(GL_FRONT_AND_BACK);
         break;
      }
      glEnable(GL_CULL_FACE);
   } else
      glDisable(GL_CULL_FACE);
   
   if (state->light_twoside)
      glEnable(GL_VERTEX_PROGRAM_TWO_SIDE);
   else
      glDisable(GL_VERTEX_PROGRAM_TWO_SIDE);

   if (state->clip_plane_enable != grend_state.hw_rs_state.clip_plane_enable) {
      grend_state.hw_rs_state.clip_plane_enable = state->clip_plane_enable;
      for (i = 0; i < 8; i++) {
         if (state->clip_plane_enable & (1 << i))
            glEnable(GL_CLIP_PLANE0 + i);
         else
            glDisable(GL_CLIP_PLANE0 + i);
      }
   }

   glLineStipple(state->line_stipple_factor, state->line_stipple_pattern);
   if (state->line_stipple_enable)
      glEnable(GL_LINE_STIPPLE);
   else
      glDisable(GL_LINE_STIPPLE);

   if (state->line_smooth)
      glEnable(GL_LINE_SMOOTH);
   else
      glDisable(GL_LINE_SMOOTH);

   if (state->poly_smooth)
      glEnable(GL_POLYGON_SMOOTH);
   else
      glDisable(GL_POLYGON_SMOOTH);

   if (state->clamp_vertex_color)
      glClampColor(GL_CLAMP_VERTEX_COLOR_ARB, GL_TRUE);
   else
      glClampColor(GL_CLAMP_VERTEX_COLOR_ARB, GL_FALSE);

   if (state->clamp_fragment_color)
      glClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_TRUE);
   else
      glClampColor(GL_CLAMP_FRAGMENT_COLOR_ARB, GL_FALSE);

   if (grend_state.have_multisample) {
      if (state->multisample) {
         glEnable(GL_MULTISAMPLE);
         glEnable(GL_SAMPLE_MASK);
      } else {
         glDisable(GL_MULTISAMPLE);
         glDisable(GL_SAMPLE_MASK);
      }
   }
}

void grend_object_bind_rasterizer(struct grend_context *ctx,
                                  uint32_t handle)
{
   struct pipe_rasterizer_state *state;

   if (handle == 0) {
      memset(&ctx->rs_state, 0, sizeof(ctx->rs_state));
      return;
   }

   state = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_RASTERIZER);
   
   if (!state) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handle);
      return;
   }

   ctx->rs_state = *state;
   ctx->scissor_state_dirty = TRUE;
   grend_hw_emit_rs(ctx);
}

static GLuint convert_wrap(int wrap)
{
   switch(wrap){
   case PIPE_TEX_WRAP_REPEAT: return GL_REPEAT;
   case PIPE_TEX_WRAP_CLAMP: return GL_CLAMP;

   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER: return GL_CLAMP_TO_BORDER;

   case PIPE_TEX_WRAP_MIRROR_REPEAT: return GL_MIRRORED_REPEAT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP: return GL_MIRROR_CLAMP_EXT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE: return GL_MIRROR_CLAMP_TO_EDGE_EXT;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER: return GL_MIRROR_CLAMP_TO_BORDER_EXT;
   default:
      assert(0);
      return -1;
   }
} 

void grend_bind_sampler_states(struct grend_context *ctx,
                               uint32_t shader_type,
                               uint32_t start_slot,
                               uint32_t num_states,
                               uint32_t *handles)
{
   int i;
   struct pipe_sampler_state *state;

   ctx->num_sampler_states[shader_type] = num_states;

   for (i = 0; i < num_states; i++) {
      if (handles[i] == 0)
         state = NULL;
      else
         state = vrend_object_lookup(ctx->object_hash, handles[i], VIRGL_OBJECT_SAMPLER_STATE);
      
      ctx->sampler_state[shader_type][i + start_slot] = state;
   }
   ctx->sampler_state_dirty = TRUE;
}

static inline GLenum convert_mag_filter(unsigned int filter)
{
   if (filter == PIPE_TEX_FILTER_NEAREST)
      return GL_NEAREST;
   return GL_LINEAR;
}

static inline GLenum convert_min_filter(unsigned int filter, unsigned int mip_filter)
{
   if (mip_filter == PIPE_TEX_MIPFILTER_NONE)
      return convert_mag_filter(filter);
   else if (mip_filter == PIPE_TEX_MIPFILTER_LINEAR) {
      if (filter == PIPE_TEX_FILTER_NEAREST)
         return GL_NEAREST_MIPMAP_LINEAR;
      else
         return GL_LINEAR_MIPMAP_LINEAR;
   } else if (mip_filter == PIPE_TEX_MIPFILTER_NEAREST) {
      if (filter == PIPE_TEX_FILTER_NEAREST)
         return GL_NEAREST_MIPMAP_NEAREST;
      else
         return GL_LINEAR_MIPMAP_NEAREST;
   }
   assert(0);
   return 0;
}

static void grend_apply_sampler_state(struct grend_context *ctx, 
                                      struct grend_resource *res,
                                      uint32_t shader_type,
                                      int id)
{
   struct grend_texture *tex = (struct grend_texture *)res;
   struct pipe_sampler_state *state = ctx->sampler_state[shader_type][id];
   bool set_all = FALSE;
   GLenum target = tex->base.target;

   if (!state) {
      fprintf(stderr, "cannot find sampler state for %d %d\n", shader_type, id);
      return;
   }
   if (res->base.nr_samples > 1) {
      tex->state = *state;
      return;
   }

   if (tex->state.max_lod == -1)
      set_all = TRUE;

   if (tex->state.wrap_s != state->wrap_s || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_S, convert_wrap(state->wrap_s));
   if (tex->state.wrap_t != state->wrap_t || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_T, convert_wrap(state->wrap_t));
   if (tex->state.wrap_r != state->wrap_r || set_all)
      glTexParameteri(target, GL_TEXTURE_WRAP_R, convert_wrap(state->wrap_r));
   if (tex->state.min_img_filter != state->min_img_filter ||
       tex->state.min_mip_filter != state->min_mip_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MIN_FILTER, convert_min_filter(state->min_img_filter, state->min_mip_filter));
   if (tex->state.mag_img_filter != state->mag_img_filter || set_all)
      glTexParameterf(target, GL_TEXTURE_MAG_FILTER, convert_mag_filter(state->mag_img_filter));
   if (res->target != GL_TEXTURE_RECTANGLE) {
      if (tex->state.min_lod != state->min_lod || set_all)
         glTexParameterf(target, GL_TEXTURE_MIN_LOD, state->min_lod);
      if (tex->state.max_lod != state->max_lod || set_all)
         glTexParameterf(target, GL_TEXTURE_MAX_LOD, state->max_lod);
      if (tex->state.lod_bias != state->lod_bias || set_all)
         glTexParameterf(target, GL_TEXTURE_LOD_BIAS, state->lod_bias);
   }

   if (tex->state.compare_mode != state->compare_mode || set_all)
      glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, state->compare_mode ? GL_COMPARE_R_TO_TEXTURE : GL_NONE);
   if (tex->state.compare_func != state->compare_func || set_all)
      glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_NEVER + state->compare_func);

   if (memcmp(&tex->state.border_color, &state->border_color, 16) || set_all)
      glTexParameterIuiv(target, GL_TEXTURE_BORDER_COLOR, state->border_color.ui);
   tex->state = *state;
}

void grend_flush(struct grend_context *ctx)
{
   glFlush();
}

void grend_flush_frontbuffer(uint32_t res_handle)
{
}

static GLenum tgsitargettogltarget(const enum pipe_texture_target target, int nr_samples)
{
   switch(target) {
   case PIPE_TEXTURE_1D:
      return GL_TEXTURE_1D;
   case PIPE_TEXTURE_2D:
      return (nr_samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
   case PIPE_TEXTURE_3D:
      return GL_TEXTURE_3D;
   case PIPE_TEXTURE_RECT:
      return GL_TEXTURE_RECTANGLE_NV;
   case PIPE_TEXTURE_CUBE:
      return GL_TEXTURE_CUBE_MAP;

   case PIPE_TEXTURE_1D_ARRAY:
      return GL_TEXTURE_1D_ARRAY;
   case PIPE_TEXTURE_2D_ARRAY:
      return (nr_samples > 1) ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
   case PIPE_TEXTURE_CUBE_ARRAY:
      return GL_TEXTURE_CUBE_MAP_ARRAY;
   case PIPE_BUFFER:
   default:
      return PIPE_BUFFER;
   }
   return PIPE_BUFFER;
}

static int inited;

void graw_renderer_init(struct grend_if_cbs *cbs)
{
   if (!inited) {
      inited = 1;
      vrend_object_init_resource_table();
      clicbs = cbs;
   }
   int gl_ver = epoxy_gl_version();
#define glewIsSupported epoxy_has_gl_extension
   if (glewIsSupported("GL_ARB_robustness"))
      grend_state.have_robustness = TRUE;
   else
      fprintf(stderr,"WARNING: running without ARB robustness in place may crash\n");

   if (gl_ver >= 33 || glewIsSupported("GL_ARB_shader_bit_encoding"))
      grend_state.have_bit_encoding = TRUE;
   if (gl_ver >= 31)
      grend_state.have_gl_prim_restart = TRUE;
   else if (glewIsSupported("GL_NV_primitive_restart"))
      grend_state.have_nv_prim_restart = TRUE;
   
   if (glewIsSupported("GL_EXT_framebuffer_multisample") && glewIsSupported("GL_ARB_texture_multisample")) {
      grend_state.have_multisample = true;
   }

   /* callbacks for when we are cleaning up the object table */
   vrend_object_set_destroy_callback(VIRGL_OBJECT_QUERY, grend_destroy_query_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_SURFACE, grend_destroy_surface_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_VS, grend_destroy_shader_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_FS, grend_destroy_shader_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_SAMPLER_VIEW, grend_destroy_sampler_view_object);
   vrend_object_set_destroy_callback(VIRGL_OBJECT_STREAMOUT_TARGET, grend_destroy_so_target_object);

   vrend_build_format_list();
   grend_state.viewport_dirty = grend_state.scissor_dirty = TRUE;
   grend_state.program_id = (GLuint)-1;
   list_inithead(&grend_state.fence_list);
   list_inithead(&grend_state.waiting_query_list);

   graw_cursor_init(&grend_state.cursor_info);
   /* create 0 context */
   graw_renderer_context_create_internal(0, 0, NULL);
}

void
graw_renderer_fini(void)
{
   if (!inited)
      return;

   vrend_object_fini_resource_table();
   inited = 0;
}

bool grend_destroy_context(struct grend_context *ctx)
{
   bool switch_0 = (ctx == grend_state.current_ctx);
   int i;

   if (switch_0) {
      grend_state.current_ctx = NULL;
      grend_state.current_hw_ctx = NULL;
   }

   /* reset references on framebuffers */
   grend_set_framebuffer_state(ctx, 0, NULL, 0);

   grend_set_num_sampler_views(ctx, PIPE_SHADER_VERTEX, 0, 0);
   grend_set_num_sampler_views(ctx, PIPE_SHADER_FRAGMENT, 0, 0);

   grend_set_streamout_targets(ctx, 0, 0, NULL);
   grend_set_num_vbo(ctx, 0);

   grend_set_index_buffer(ctx, 0, 0, 0);

   if (ctx->fb_id)
      glDeleteFramebuffers(1, &ctx->fb_id);

   if (ctx->blit_fb_ids[0])
      glDeleteFramebuffers(2, ctx->blit_fb_ids);

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

   while (ctx->enabled_attribs_bitmask) {
      i = u_bit_scan(&ctx->enabled_attribs_bitmask);
      
      glDisableVertexAttribArray(i);
   }

   grend_bind_va(0);

   glDeleteVertexArrays(1, &ctx->vaoid);

   grend_free_programs(ctx);

   /* need to free any objects still in hash table - TODO */
   vrend_object_fini_ctx_table(ctx->object_hash);

   if (ctx->ctx_id != 0)
      clicbs->destroy_gl_context(ctx->gl_context);

   FREE(ctx);

   return switch_0;
}

struct grend_context *grend_create_context(int id, uint32_t nlen, const char *debug_name)
{
   struct grend_context *grctx = CALLOC_STRUCT(grend_context);

   if (nlen) {
      strncpy(grctx->debug_name, debug_name, 64);
   }

   grctx->gl_context = clicbs->create_gl_context(0);
   clicbs->make_current(0, grctx->gl_context);

   grctx->ctx_id = id;
   list_inithead(&grctx->programs);
   list_inithead(&grctx->active_nontimer_query_list);
   glGenVertexArrays(1, &grctx->vaoid);
   glGenFramebuffers(1, &grctx->fb_id);
   glGenFramebuffers(2, grctx->blit_fb_ids);
   grctx->object_hash = vrend_object_init_ctx_table();

   grend_bind_va(grctx->vaoid);
   return grctx;
}

int graw_renderer_resource_attach_iov(int res_handle, struct virgl_iovec *iov,
                                      int num_iovs)
{
   struct grend_resource *res;
   
   res = vrend_resource_lookup(res_handle, 0);
   if (!res)
      return;

   /* work out size and max resource size */
   res->iov = iov;
   res->num_iovs = num_iovs;
   return 0;
}

void graw_renderer_resource_invalid_iov(int res_handle)
{
   struct grend_resource *res;
   res = vrend_resource_lookup(res_handle, 0);
   if (!res) {
      return;
   }

   (*clicbs->inval_backing)(res->iov, res->num_iovs);
   res->num_iovs = 0;
   res->iov = 0;
}

void graw_renderer_resource_create(struct graw_renderer_resource_create_args *args, struct virgl_iovec *iov, uint32_t num_iovs)
{
   struct grend_resource *gr = (struct grend_resource *)CALLOC_STRUCT(grend_texture);
   int level;

   gr->handle = args->handle;
   gr->iov = iov;
   gr->num_iovs = num_iovs;
   gr->base.width0 = args->width;
   gr->base.height0 = args->height;
   gr->base.depth0 = args->depth;
   gr->base.format = args->format;
   gr->base.target = args->target;
   gr->base.last_level = args->last_level;
   gr->base.nr_samples = args->nr_samples;
   gr->base.array_size = args->array_size;

   if (args->flags & VIRGL_RESOURCE_Y_0_TOP)
      gr->y_0_top = TRUE;

   pipe_reference_init(&gr->base.reference, 1);

   if (args->bind == PIPE_BIND_CUSTOM) {
      /* custom shuold only be for buffers */
      gr->ptr = malloc(args->width);
   } else if (args->bind == PIPE_BIND_INDEX_BUFFER) {
      gr->target = GL_ELEMENT_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->bind == PIPE_BIND_STREAM_OUTPUT) {
      gr->target = GL_TRANSFORM_FEEDBACK_BUFFER;
      glGenBuffersARB(1, &gr->id);
      glBindBuffer(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else if (args->target == PIPE_BUFFER) {
      gr->target = GL_ARRAY_BUFFER_ARB;
      glGenBuffersARB(1, &gr->id);
      glBindBufferARB(gr->target, gr->id);
      glBufferData(gr->target, args->width, NULL, GL_STREAM_DRAW);
   } else {
      struct grend_texture *gt = (struct grend_texture *)gr;
      GLenum internalformat, glformat, gltype;
      gr->target = tgsitargettogltarget(args->target, args->nr_samples);
      glGenTextures(1, &gr->id);
      glBindTexture(gr->target, gr->id);

      internalformat = tex_conv_table[args->format].internalformat;
      glformat = tex_conv_table[args->format].glformat;
      gltype = tex_conv_table[args->format].gltype;
      if (internalformat == 0) {
         fprintf(stderr,"unknown format is %d\n", args->format);
         return;
      }

      if (args->nr_samples > 1) {
         if (gr->target == GL_TEXTURE_2D_MULTISAMPLE) {
            glTexImage2DMultisample(gr->target, args->nr_samples,
                                    internalformat, args->width, args->height,
                                    TRUE);
         } else {
            glTexImage3DMultisample(gr->target, args->nr_samples,
                                    internalformat, args->width, args->height, args->array_size,
                                    TRUE);
         }

      } else if (gr->target == GL_TEXTURE_CUBE_MAP) {
         int i;
         for (i = 0; i < 6; i++) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + i;
            for (level = 0; level <= args->last_level; level++) {
               unsigned mwidth = u_minify(args->width, level);
               unsigned mheight = u_minify(args->height, level);
               glTexImage2D(ctarget, level, internalformat, mwidth, mheight, 0, glformat,
                            gltype, NULL);
            }
         }
      } else if (gr->target == GL_TEXTURE_3D || gr->target == GL_TEXTURE_2D_ARRAY || gr->target == GL_TEXTURE_CUBE_MAP_ARRAY) {
         for (level = 0; level <= args->last_level; level++) {
            unsigned depth_param = (gr->target == GL_TEXTURE_2D_ARRAY || gr->target == GL_TEXTURE_CUBE_MAP_ARRAY) ? args->array_size : u_minify(args->depth, level);
            unsigned mwidth = u_minify(args->width, level);
            unsigned mheight = u_minify(args->height, level);
            glTexImage3D(gr->target, level, internalformat, mwidth, mheight, depth_param, 0,
                         glformat,
                         gltype, NULL);
         }
      } else if (gr->target == GL_TEXTURE_1D) {
         for (level = 0; level <= args->last_level; level++) {
            unsigned mwidth = u_minify(args->width, level);
            glTexImage1D(gr->target, level, internalformat, mwidth, 0,
                         glformat,
                         gltype, NULL);
         }
      } else {
         for (level = 0; level <= args->last_level; level++) {
            unsigned mwidth = u_minify(args->width, level);
            unsigned mheight = u_minify(args->height, level);
            glTexImage2D(gr->target, level, internalformat, mwidth, gr->target == GL_TEXTURE_1D_ARRAY ? args->array_size : mheight, 0, glformat,
                         gltype, NULL);
         }
      }

      gt->state.max_lod = -1;
      gt->cur_swizzle_r = gt->cur_swizzle_g = gt->cur_swizzle_b = gt->cur_swizzle_a = -1;
   }

   vrend_resource_insert(gr, sizeof(*gr), args->handle);
}

void graw_renderer_resource_destroy(struct grend_resource *res)
{
   if (res->scannedout)
      (*clicbs->scanout_resource_info)(0, res->id, 0, 0, 0, 0, 0);

   if (res->readback_fb_id)
      glDeleteFramebuffers(1, &res->readback_fb_id);

   if (res->ptr)
      free(res->ptr);
   if (res->id) {
      if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
          res->target == GL_ARRAY_BUFFER_ARB ||
          res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
         glDeleteBuffers(1, &res->id);
      } else
         glDeleteTextures(1, &res->id);
   }

   if (res->handle)
      vrend_resource_remove(res->handle);
   free(res);
}


void graw_renderer_resource_unref(uint32_t res_handle)
{
   struct grend_resource *res;

   res = vrend_resource_lookup(res_handle, 0);
   if (!res)
      return;

   if (res->iov) {
      (*clicbs->inval_backing)(res->iov, res->num_iovs);
   }
   vrend_resource_remove(res->handle);
   res->handle = 0;

   grend_resource_reference(&res, NULL);
}

static int use_sub_data = 0;
struct virgl_sub_upload_data {
   GLenum target;
   struct pipe_box *box;
};

static void iov_buffer_upload(void *cookie, uint32_t doff, void *src, int len)
{
   struct virgl_sub_upload_data *d = cookie;
   glBufferSubData(d->target, d->box->x + doff, len, src);
}

static void copy_transfer_data(struct pipe_resource *res,
                               struct virgl_iovec *iov,
                               unsigned int num_iovs,
                               void *data,
                               uint32_t src_stride,
                               struct pipe_box *box,
                               uint64_t offset)
{
   int blsize = util_format_get_blocksize(res->format);
   GLuint size = graw_iov_size(iov, num_iovs);
   GLuint send_size = util_format_get_nblocks(res->format, box->width,
                                              box->height) * blsize * box->depth;
   GLuint bwx = util_format_get_nblocksx(res->format, box->width) * blsize;
   GLuint bh = util_format_get_nblocksy(res->format, box->height);
   int h;
   uint32_t myoffset = offset;

   if (send_size == size || bh == 1)
      graw_iov_to_buf(iov, num_iovs, offset, data, send_size);
   else {
      for (h = 0; h < bh; h++) {
         void *ptr = data + (h * bwx);
         graw_iov_to_buf(iov, num_iovs, myoffset, ptr, bwx);
         myoffset += src_stride;
      }
   }
}

void graw_renderer_transfer_write_iov(uint32_t res_handle,
                                      uint32_t ctx_id,
                                      int level,
                                      uint32_t stride,
                                      uint32_t layer_stride,
                                      struct pipe_box *box,
                                      uint64_t offset,
                                      struct virgl_iovec *iov,
                                      unsigned int num_iovs)
{
   struct grend_resource *res;

   void *data;

   res = vrend_resource_lookup(res_handle, ctx_id);
   if (res == NULL) {
      struct grend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   if ((res->iov && !iov) || num_iovs == 0) {
      iov = res->iov;
      num_iovs = res->num_iovs;
   }

   if (!iov) {
      struct grend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   grend_hw_switch_context(vrend_lookup_renderer_ctx(0), TRUE);

   if (res->target == 0 && res->ptr) {
      graw_iov_to_buf(iov, num_iovs, offset, res->ptr + box->x, box->width);
      return;
   }
   if (res->target == GL_TRANSFORM_FEEDBACK_BUFFER ||
       res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB) {
      struct virgl_sub_upload_data d;
      d.box = box;
      d.target = res->target;

      glBindBufferARB(res->target, res->id);
      if (use_sub_data == 1) {
         graw_iov_to_buf_cb(iov, num_iovs, offset, box->width, &iov_buffer_upload, &d);
      } else {
         data = glMapBufferRange(res->target, box->x, box->width, GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_WRITE_BIT);
         if (data == NULL) {
            fprintf(stderr,"map failed for element buffer\n");
            graw_iov_to_buf_cb(iov, num_iovs, offset, box->width, &iov_buffer_upload, &d);
         } else {
            graw_iov_to_buf(iov, num_iovs, offset, data, box->width);
            glUnmapBuffer(res->target);
         }
      }
   } else {
      GLenum glformat;
      GLenum gltype;
      int need_temp = 0;
      int elsize = util_format_get_blocksize(res->base.format);
      int x = 0, y = 0;
      boolean compressed;
      grend_use_program(0);

      if (!stride)
         stride = util_format_get_nblocksx(res->base.format, u_minify(res->base.width0, level)) * elsize;

      compressed = util_format_is_compressed(res->base.format);
      if (num_iovs > 1 || compressed) {
         need_temp = 1;
      }

      if (need_temp) {
         GLuint send_size = util_format_get_nblocks(res->base.format, box->width,
                                                    box->height) * util_format_get_blocksize(res->base.format) * box->depth;
         data = malloc(send_size);
         copy_transfer_data(&res->base, iov, num_iovs, data, stride,
                            box, offset);
      } else {
         data = iov[0].iov_base + offset;
      }

      if (stride && !need_temp) {
         glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / elsize);
      } else
         glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

      switch (elsize) {
      case 1:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         break;
      case 2:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
         break;
      case 4:
      default:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
         break;
      case 8:
         glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
         break;
      }

      glformat = tex_conv_table[res->base.format].glformat;
      gltype = tex_conv_table[res->base.format].gltype; 

      if (res->is_front || res->y_0_top) {
         if (!res->is_front) {
            if (res->readback_fb_id == 0 || res->readback_fb_level != level) {
               GLuint fb_id;
               if (res->readback_fb_id)
                  glDeleteFramebuffers(1, &res->readback_fb_id);
            
               glGenFramebuffers(1, &fb_id);
               glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);
               grend_fb_bind_texture(res, 0, level, 0);

               res->readback_fb_id = fb_id;
               res->readback_fb_level = level;
            } else {
               glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
            }
            glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
         } else {

            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
            glDrawBuffer(GL_BACK);
         }
         grend_blend_enable(GL_FALSE);
         grend_depth_test_enable(GL_FALSE);
         grend_alpha_test_enable(GL_FALSE);
         grend_stencil_test_enable(GL_FALSE);
         glPixelZoom(1.0f, res->y_0_top ? -1.0f : 1.0f);
         glWindowPos2i(box->x, res->y_0_top ? res->base.height0 - box->y : box->y);
         glDrawPixels(box->width, box->height, glformat, gltype,
                      data);
      } else {
         uint32_t comp_size;
         glBindTexture(res->target, res->id);

         if (compressed) {
            glformat = tex_conv_table[res->base.format].internalformat;
            comp_size = util_format_get_nblocks(res->base.format, box->width,
                                                box->height) * util_format_get_blocksize(res->base.format);
         }

         if (glformat == 0) {
            glformat = GL_BGRA;
            gltype = GL_UNSIGNED_BYTE;
         }

         x = box->x;
         y = box->y;

         if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
            /* we get values from the guest as 24-bit scaled integers
               but we give them to the host GL and it interprets them
               as 32-bit scaled integers, so we need to scale them here */
            glPixelTransferf(GL_DEPTH_SCALE, 256.0);
         }
         if (res->target == GL_TEXTURE_CUBE_MAP) {
            GLenum ctarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + box->z;
            if (compressed) {
               glCompressedTexSubImage2D(ctarget, level, x, y,
                                         box->width, box->height,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage2D(ctarget, level, x, y, box->width, box->height,
                               glformat, gltype, data);
            }
         } else if (res->target == GL_TEXTURE_3D || res->target == GL_TEXTURE_2D_ARRAY || res->target == GL_TEXTURE_CUBE_MAP_ARRAY) {
            if (compressed) {
               glCompressedTexSubImage3D(res->target, level, x, y, box->z,
                                         box->width, box->height, box->depth,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage3D(res->target, level, x, y, box->z,
                               box->width, box->height, box->depth,
                               glformat, gltype, data);
            }
         } else if (res->target == GL_TEXTURE_1D) {
            if (compressed) {
               glCompressedTexSubImage1D(res->target, level, box->x,
                                         box->width,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage1D(res->target, level, box->x, box->width,
                               glformat, gltype, data);
            }
         } else {
            if (compressed) {
               glCompressedTexSubImage2D(res->target, level, x, res->target == GL_TEXTURE_1D_ARRAY ? box->z : y,
                                         box->width, box->height,
                                         glformat, comp_size, data);
            } else {
               glTexSubImage2D(res->target, level, x, res->target == GL_TEXTURE_1D_ARRAY ? box->z : y,
                               box->width, box->height,
                               glformat, gltype, data);
            }
         }
         if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
            glPixelTransferf(GL_DEPTH_SCALE, 1.0);
         }
      }
      if (stride && !need_temp)
         glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

      if (need_temp)
         free(data);
   }

}

static void vrend_transfer_send_getteximage(struct grend_resource *res,
                                            uint32_t level, uint32_t stride,
                                            struct pipe_box *box, uint64_t offset,
                                            struct virgl_iovec *iov, int num_iovs)
{
   GLenum format, type;
   uint32_t send_size, tex_size;
   void *data;
   int elsize = util_format_get_blocksize(res->base.format);
   int compressed = util_format_is_compressed(res->base.format);
   GLenum target;
   uint32_t depth = 1;
   uint32_t send_offset = 0;
   format = tex_conv_table[res->base.format].glformat;
   type = tex_conv_table[res->base.format].gltype; 

   if (compressed)
      format = tex_conv_table[res->base.format].internalformat;

   if (res->target == GL_TEXTURE_3D)
      depth = u_minify(res->base.depth0, level);
   else if (res->target == GL_TEXTURE_2D_ARRAY || res->target == GL_TEXTURE_1D_ARRAY || res->target == GL_TEXTURE_CUBE_MAP_ARRAY)
      depth = res->base.array_size;

   tex_size = util_format_get_nblocks(res->base.format, u_minify(res->base.width0, level), u_minify(res->base.height0, level)) * util_format_get_blocksize(res->base.format) * depth;
   
   send_size = util_format_get_nblocks(res->base.format, box->width, box->height) * util_format_get_blocksize(res->base.format) * box->depth;

   if (box->z && res->target != GL_TEXTURE_CUBE_MAP) {
      send_offset = util_format_get_nblocks(res->base.format, u_minify(res->base.width0, level), u_minify(res->base.height0, level)) * util_format_get_blocksize(res->base.format) * box->z;
   }

   data = malloc(tex_size);
   if (!data)
      return;

   switch (elsize) {
   case 1:
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_PACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_PACK_ALIGNMENT, 8);
      break;
   }

   glBindTexture(res->target, res->id);
   if (res->target == GL_TEXTURE_CUBE_MAP) {
      target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + box->z;
   } else
      target = res->target;
      
   if (compressed) {
      if (grend_state.have_robustness)
         glGetnCompressedTexImageARB(target, level, tex_size, data);
      else
         glGetCompressedTexImage(target, level, data);
   } else {
      if (grend_state.have_robustness)
         glGetnTexImageARB(target, level, format, type, tex_size, data);
      else
         glGetTexImage(target, level, format, type, data);
   }
      
   glPixelStorei(GL_PACK_ALIGNMENT, 4);

   graw_transfer_write_tex_return(&res->base, box, level, stride, offset, iov, num_iovs, data + send_offset, send_size, FALSE);
   free(data);
}

static void vrend_transfer_send_readpixels(struct grend_resource *res,
                                           uint32_t level, uint32_t stride,
                                           struct pipe_box *box, uint64_t offset,
                                           struct virgl_iovec *iov, int num_iovs)
{
   void *myptr = iov[0].iov_base + offset;
   int need_temp = 0;
   GLuint fb_id;
   void *data;
   boolean actually_invert, separate_invert = FALSE;
   GLenum format, type;
   GLint y1;
   uint32_t send_size = 0;
   uint32_t h = u_minify(res->base.height0, level);
   int elsize = util_format_get_blocksize(res->base.format);

   grend_use_program(0);

   format = tex_conv_table[res->base.format].glformat;
   type = tex_conv_table[res->base.format].gltype; 
   /* if we are asked to invert and reading from a front then don't */

   if (res->is_front)
      actually_invert = FALSE;
   else
      actually_invert = res->y_0_top;

   if (actually_invert && !have_invert_mesa)
      separate_invert = TRUE;

   if (num_iovs > 1 || separate_invert)
      need_temp = 1;

   send_size = box->width * box->height * box->depth * util_format_get_blocksize(res->base.format);

   if (need_temp) {
      data = malloc(send_size);
      if (!data)
         fprintf(stderr,"malloc failed %d\n", send_size);
   } else
      data = myptr;
//      fprintf(stderr,"TEXTURE TRANSFER %d %d %d %d %d, temp:%d\n", res_handle, res->readback_fb_id, box->width, box->height, level, need_temp);

   if (!res->is_front) {
      if (res->readback_fb_id == 0 || res->readback_fb_level != level || res->readback_fb_z != box->z) {

         if (res->readback_fb_id)
            glDeleteFramebuffers(1, &res->readback_fb_id);
         
         glGenFramebuffers(1, &fb_id);
         glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fb_id);

         grend_fb_bind_texture(res, 0, level, box->z);

         res->readback_fb_id = fb_id;
         res->readback_fb_level = level;
         res->readback_fb_z = box->z;
      } else {
         glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, res->readback_fb_id);
      }
      if (actually_invert)
         y1 = h - box->y - box->height;
      else
         y1 = box->y;
      
      if (have_invert_mesa && actually_invert)
         glPixelStorei(GL_PACK_INVERT_MESA, 1);
      if (!vrend_format_is_ds(res->base.format))
          glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
      if (!need_temp && stride)
         glPixelStorei(GL_PACK_ROW_LENGTH, stride);
   } else {
      glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
      y1 = box->y;
      glReadBuffer(GL_BACK);
   }

   switch (elsize) {
   case 1:
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_PACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_PACK_ALIGNMENT, 8);
      break;
   }  

   if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM) {
      /* we get values from the guest as 24-bit scaled integers
         but we give them to the host GL and it interprets them
         as 32-bit scaled integers, so we need to scale them here */
      glPixelTransferf(GL_DEPTH_SCALE, 1.0/256.0);
   }
   if (grend_state.have_robustness)
      glReadnPixelsARB(box->x, y1, box->width, box->height, format, type, send_size, data);
   else
      glReadPixels(box->x, y1, box->width, box->height, format, type, data);

   if (res->base.format == (enum pipe_format)VIRGL_FORMAT_Z24X8_UNORM)
      glPixelTransferf(GL_DEPTH_SCALE, 1.0);
   if (have_invert_mesa && actually_invert)
      glPixelStorei(GL_PACK_INVERT_MESA, 0);
   if (!need_temp && stride)
      glPixelStorei(GL_PACK_ROW_LENGTH, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 4);
   if (need_temp) {
      graw_transfer_write_tex_return(&res->base, box, level, stride, offset, iov, num_iovs, data, send_size, separate_invert);
      free(data);
   }
}

void graw_renderer_transfer_send_iov(uint32_t res_handle, uint32_t ctx_id,
                                     uint32_t level, uint32_t stride,
                                     uint32_t layer_stride,
                                     struct pipe_box *box,
                                     uint64_t offset, struct virgl_iovec *iov,
                                     int num_iovs)
{
   struct grend_resource *res;
   struct grend_context *ctx = vrend_lookup_renderer_ctx(ctx_id);

   res = vrend_resource_lookup(res_handle, ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   if (box->width + box->x > u_minify(res->base.width0, level) ||
       box->height + box->y > u_minify(res->base.height0, level))
       return;

   grend_hw_switch_context(vrend_lookup_renderer_ctx(0), TRUE);

   if (res->iov && (!iov || num_iovs == 0)) {
      iov = res->iov;
      num_iovs = res->num_iovs;
   }

   if (!iov) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   if (res->target == 0 && res->ptr) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      graw_transfer_write_return(res->ptr + box->x, send_size, offset, iov, num_iovs);
   } else if (res->target == GL_ELEMENT_ARRAY_BUFFER_ARB ||
       res->target == GL_ARRAY_BUFFER_ARB ||
       res->target == GL_TRANSFORM_FEEDBACK_BUFFER) {
      uint32_t send_size = box->width * util_format_get_blocksize(res->base.format);      
      void *data;
      glBindBufferARB(res->target, res->id);
      data = glMapBufferRange(res->target, box->x, box->width, GL_MAP_READ_BIT);
      if (!data)
         fprintf(stderr,"unable to open buffer for reading %d\n", res->target);
      else
         graw_transfer_write_return(data, send_size, offset, iov, num_iovs);
      glUnmapBuffer(res->target);
   } else {
      boolean can_readpixels = TRUE;

      can_readpixels = vrend_format_can_render(res->base.format) || vrend_format_is_ds(res->base.format);

      if (can_readpixels) {
         vrend_transfer_send_readpixels(res, level, stride, box, offset,
                                        iov, num_iovs);
         return;
      }

      vrend_transfer_send_getteximage(res, level, stride, box, offset,
                                      iov, num_iovs);

   }
}

void grend_set_stencil_ref(struct grend_context *ctx,
                           struct pipe_stencil_ref *ref)
{
   if (ctx->stencil_refs[0] != ref->ref_value[0] ||
       ctx->stencil_refs[1] != ref->ref_value[1]) {
      ctx->stencil_refs[0] = ref->ref_value[0];
      ctx->stencil_refs[1] = ref->ref_value[1];
      ctx->stencil_state_dirty = TRUE;
   }
   
}

static void grend_hw_emit_blend_color(struct grend_context *ctx)
{
   struct pipe_blend_color *color = &ctx->blend_color;
   glBlendColor(color->color[0], color->color[1], color->color[2],
                color->color[3]);
}

void grend_set_blend_color(struct grend_context *ctx,
                           struct pipe_blend_color *color)
{
   ctx->blend_color = *color;
   grend_hw_emit_blend_color(ctx);
}

void grend_set_scissor_state(struct grend_context *ctx,
                             struct pipe_scissor_state *ss)
{
   ctx->ss = *ss;
   ctx->scissor_state_dirty = TRUE;
}

void grend_set_polygon_stipple(struct grend_context *ctx,
                               struct pipe_poly_stipple *ps)
{
   glPolygonStipple((const GLubyte *)ps->stipple);
}

void grend_set_clip_state(struct grend_context *ctx, struct pipe_clip_state *ucp)
{
   int i, j;
   GLdouble val[4];

   for (i = 0; i < 8; i++) {
      for (j = 0; j < 4; j++)
         val[j] = ucp->ucp[i][j];
      glClipPlane(GL_CLIP_PLANE0 + i, val);
   }
}

void grend_set_sample_mask(struct grend_context *ctx, unsigned sample_mask)
{
   glSampleMaski(0, sample_mask);
}


static void grend_hw_emit_streamout_targets(struct grend_context *ctx)
{
   int i;

   for (i = 0; i < ctx->num_so_targets; i++) {
      if (ctx->so_targets[i]->buffer_offset)
         glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, i, ctx->so_targets[i]->buffer->id, ctx->so_targets[i]->buffer_offset, ctx->so_targets[i]->buffer_size);
      else
         glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, i, ctx->so_targets[i]->buffer->id);
   }
}

void grend_set_streamout_targets(struct grend_context *ctx,
                                 uint32_t append_bitmask,
                                 uint32_t num_targets,
                                 uint32_t *handles)
{
   struct grend_so_target *target;
   int i;
   int old_num = ctx->num_so_targets;

   ctx->num_so_targets = num_targets;
   for (i = 0; i < num_targets; i++) {
      target = vrend_object_lookup(ctx->object_hash, handles[i], VIRGL_OBJECT_STREAMOUT_TARGET);
      if (!target) {
         report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_HANDLE, handles[i]);
         return;
      }
      grend_so_target_reference(&ctx->so_targets[i], target);
   }

   for (i = num_targets; i < old_num; i++)
      grend_so_target_reference(&ctx->so_targets[i], NULL);

   grend_hw_emit_streamout_targets(ctx);
}

static void vrend_resource_buffer_copy(struct grend_context *ctx,
                                       struct grend_resource *src_res,
                                       struct grend_resource *dst_res,
                                       uint32_t dstx, uint32_t srcx,
                                       uint32_t width)
{

   glBindBuffer(GL_COPY_READ_BUFFER, src_res->id);
   glBindBuffer(GL_COPY_WRITE_BUFFER, dst_res->id);

   glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcx, dstx, width);
   glBindBuffer(GL_COPY_READ_BUFFER, 0);
   glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

static void vrend_resource_copy_fallback(struct grend_context *ctx,
                                         struct grend_resource *src_res,
                                         struct grend_resource *dst_res,
                                         uint32_t dst_level,
                                         uint32_t dstx, uint32_t dsty,
                                         uint32_t dstz, uint32_t src_level,
                                         const struct pipe_box *src_box)
{
   void *tptr;
   uint32_t transfer_size;
   GLenum glformat, gltype;
   int elsize = util_format_get_blocksize(dst_res->base.format);
   int compressed = util_format_is_compressed(dst_res->base.format);

   if (src_res->base.format != dst_res->base.format) {
      fprintf(stderr, "copy fallback failed due to mismatched formats %d %d\n", src_res->base.format, dst_res->base.format);
      return;
   }

   /* this is ugly need to do a full GetTexImage */
   transfer_size = util_format_get_nblocks(src_res->base.format, u_minify(src_res->base.width0, src_level), u_minify(src_res->base.height0, src_level)) *
      u_minify(src_res->base.depth0, src_level) * util_format_get_blocksize(src_res->base.format);

   tptr = malloc(transfer_size);
   if (!tptr)
      return;

   glformat = tex_conv_table[src_res->base.format].glformat;
   gltype = tex_conv_table[src_res->base.format].gltype; 

   if (compressed)
      glformat = tex_conv_table[src_res->base.format].internalformat;
      
   switch (elsize) {
   case 1:
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_PACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_PACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_PACK_ALIGNMENT, 8);
      break;
   }
   glBindTexture(src_res->target, src_res->id);
   if (compressed) {
      if (grend_state.have_robustness)
         glGetnCompressedTexImageARB(src_res->target, src_level, transfer_size, tptr);
      else
         glGetCompressedTexImage(src_res->target, src_level, tptr);
   } else {
      if (grend_state.have_robustness)
         glGetnTexImageARB(src_res->target, src_level, glformat, gltype, transfer_size, tptr);
      else
         glGetTexImage(src_res->target, src_level, glformat, gltype, tptr);
   }

   glPixelStorei(GL_PACK_ALIGNMENT, 4);
   switch (elsize) {
   case 1:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      break;
   case 2:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
      break;
   case 4:
   default:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      break;
   case 8:
      glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
      break;
   }

   glBindTexture(dst_res->target, dst_res->id);
   if (compressed) {
      glCompressedTexSubImage2D(dst_res->target, dst_level, dstx, dsty,
                                src_box->width, src_box->height,
                                glformat, transfer_size, tptr);
   } else {
      glTexSubImage2D(dst_res->target, dst_level, dstx, dsty, src_box->width, src_box->height, glformat, gltype, tptr);
   }

   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   free(tptr);
}

void graw_renderer_resource_copy_region(struct grend_context *ctx,
                                        uint32_t dst_handle, uint32_t dst_level,
                                        uint32_t dstx, uint32_t dsty, uint32_t dstz,
                                        uint32_t src_handle, uint32_t src_level,
                                        const struct pipe_box *src_box)
{
   struct grend_resource *src_res, *dst_res;   
   GLbitfield glmask = 0;
   GLint sy1, sy2, dy1, dy2;

   if (ctx->in_error)
      return;

   src_res = vrend_resource_lookup(src_handle, ctx->ctx_id);
   dst_res = vrend_resource_lookup(dst_handle, ctx->ctx_id);

   if (!src_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, src_handle);
      return;
   }
   if (!dst_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, dst_handle);
      return;
   }

   if (src_res->base.target == PIPE_BUFFER && dst_res->base.target == PIPE_BUFFER) {
      /* do a buffer copy */
      vrend_resource_buffer_copy(ctx, src_res, dst_res, dstx,
                                 src_box->x, src_box->width);
      return;
   }

   if (!vrend_format_can_render(src_res->base.format) ||
       !vrend_format_can_render(dst_res->base.format)) {
      vrend_resource_copy_fallback(ctx, src_res, dst_res, dst_level, dstx,
                                   dsty, dstz, src_level, src_box);

      return;
   }

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->blit_fb_ids[0]);
   grend_fb_bind_texture(src_res, 0, src_level, src_box->z);
      
   if (!dst_res->is_front) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->blit_fb_ids[1]);
      grend_fb_bind_texture(dst_res, 0, dst_level, dstz);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->blit_fb_ids[1]);
   } else
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->blit_fb_ids[0]);

   glmask = GL_COLOR_BUFFER_BIT;
   glDisable(GL_SCISSOR_TEST);

   if (!src_res->is_front && !src_res->y_0_top) {
      sy1 = src_box->y;
      sy2 = src_box->y + src_box->height;
   } else {
      sy1 = src_res->base.height0 - src_box->y - src_box->height;
      sy2 = src_res->base.height0 - src_box->y;
   }

   if (!dst_res->is_front && !dst_res->y_0_top) {
      dy1 = dsty;
      dy2 = dsty + src_box->height;
   } else {
      dy1 = dst_res->base.height0 - dsty - src_box->height;
      dy2 = dst_res->base.height0 - dsty;
   }

   glBlitFramebuffer(src_box->x, sy1,
                     src_box->x + src_box->width,
                     sy2,
                     dstx, dy1,
                     dstx + src_box->width,
                     dy2,
                     glmask, GL_NEAREST);

}

static void graw_renderer_blit_int(struct grend_context *ctx,
                                   uint32_t dst_handle, uint32_t src_handle,
                                   const struct pipe_blit_info *info)
{
   struct grend_resource *src_res, *dst_res;
   GLbitfield glmask = 0;
   int src_y1, src_y2, dst_y1, dst_y2;

   if (ctx->in_error)
      return;

   src_res = vrend_resource_lookup(src_handle, ctx->ctx_id);
   dst_res = vrend_resource_lookup(dst_handle, ctx->ctx_id);

   if (!src_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, src_handle);
      return;
   }
   if (!dst_res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, dst_handle);
      return;
   }

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->blit_fb_ids[0]);

   grend_fb_bind_texture(src_res, 0, info->src.level, info->src.box.z);

   if (!dst_res->is_front) {
      glBindFramebuffer(GL_FRAMEBUFFER_EXT, ctx->blit_fb_ids[1]);

      grend_fb_bind_texture(dst_res, 0, info->dst.level, info->dst.box.z);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->blit_fb_ids[1]);
   } else
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx->blit_fb_ids[0]);

   if (info->mask & PIPE_MASK_Z)
      glmask |= GL_DEPTH_BUFFER_BIT;
   if (info->mask & PIPE_MASK_S)
      glmask |= GL_STENCIL_BUFFER_BIT;
   if (info->mask & PIPE_MASK_RGBA)
      glmask |= GL_COLOR_BUFFER_BIT;

   if (!dst_res->is_front && !dst_res->y_0_top) {
      dst_y1 = info->dst.box.y + info->dst.box.height;
      dst_y2 = info->dst.box.y;
   } else {
      dst_y1 = dst_res->base.height0 - info->dst.box.y - info->dst.box.height;
      dst_y2 = dst_res->base.height0 - info->dst.box.y;
   }

   if ((!src_res->is_front && !src_res->y_0_top)) {
      src_y1 = info->src.box.y + info->src.box.height;
      src_y2 = info->src.box.y;
   } else {
      src_y1 = src_res->base.height0 - info->src.box.y - info->src.box.height;
      src_y2 = src_res->base.height0 - info->src.box.y;
   }

   if (info->scissor_enable) {
      glScissor(info->scissor.minx, info->scissor.miny, info->scissor.maxx - info->scissor.minx, info->scissor.maxy - info->scissor.miny);
      ctx->scissor_state_dirty = TRUE;
      glEnable(GL_SCISSOR_TEST);
   } else
      glDisable(GL_SCISSOR_TEST);
      
   glBlitFramebuffer(info->src.box.x,
                     src_y1,
                     info->src.box.x + info->src.box.width,
                     src_y2,
                     info->dst.box.x,
                     dst_y1,
                     info->dst.box.x + info->dst.box.width,
                     dst_y2,
                     glmask, convert_mag_filter(info->filter));

}

void graw_renderer_blit(struct grend_context *ctx,
                        uint32_t dst_handle, uint32_t src_handle,
                        const struct pipe_blit_info *info)
{
   graw_renderer_blit_int(ctx, dst_handle, src_handle, info);
}

int graw_renderer_set_scanout(uint32_t res_handle, uint32_t scanout_id, uint32_t ctx_id,
                              struct pipe_box *box)
{
   struct grend_resource *res;
   res = vrend_resource_lookup(res_handle, ctx_id);
   if (!res)
      return 0;

   grend_resource_reference(&frontbuffer[scanout_id], res);
   front_box[scanout_id] = *box;

   {
      int elsize = util_format_get_blocksize(res->base.format);
      uint32_t stride;
      stride = util_format_get_nblocksx(res->base.format, u_minify(res->base.width0, 0)) * elsize;
      (*clicbs->scanout_resource_info)(scanout_id, res->id, res->y_0_top ? 1 : 0, stride, res->base.width0, res->base.height0, res->base.format);
      res->scannedout = 1;
   }
   (*clicbs->scanout_rect_info)(scanout_id, res->id, box->x, box->y, box->width, box->height);
   fprintf(stderr,"setting frontbuffer %d to %d\n", scanout_id, res_handle);
   return 0;
}

int graw_renderer_flush_buffer_res(struct grend_resource *res,
                                   struct pipe_box *box)
{
   if (1 && !res->is_front) {
      int i;
      grend_hw_switch_context(vrend_lookup_renderer_ctx(0), TRUE);

      for (i = 0; i < MAX_SCANOUT; i++) {
         if (clicbs->flush_scanout)
            clicbs->flush_scanout(i, box->x, box->y, box->width, box->height);
      }
   }

   return 0;
}

int graw_renderer_flush_buffer(uint32_t res_handle,
                               uint32_t ctx_id,
                               struct pipe_box *box)
{
   struct grend_resource *res;
   int i;
   bool found = false;
   if (!localrender)
      return 0;

   res = vrend_resource_lookup(res_handle, ctx_id);
   if (!res)
      return 0;

   for (i = 0; i < MAX_SCANOUT; i++) {
      if (res == frontbuffer[i]) {
         found = true;
      }
   }

   if (found == false) {
      fprintf(stderr,"not the frontbuffer %d\n", res_handle); 
      return 0;
   }

   return graw_renderer_flush_buffer_res(res, box);
}

int graw_renderer_create_fence(int client_fence_id, uint32_t ctx_id)
{
   struct grend_fence *fence;

   fence = malloc(sizeof(struct grend_fence));
   if (!fence)
      return -1;

   fence->ctx_id = ctx_id;
   fence->fence_id = client_fence_id;
   fence->syncobj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   list_addtail(&fence->fences, &grend_state.fence_list);
   return 0;
}

void graw_renderer_check_fences(void)
{
   struct grend_fence *fence, *stor;
   uint32_t latest_id = 0;
   GLenum glret;

   if (!inited)
      return;

   LIST_FOR_EACH_ENTRY_SAFE(fence, stor, &grend_state.fence_list, fences) {
      glret = glClientWaitSync(fence->syncobj, 0, 0);
      if (glret == GL_ALREADY_SIGNALED){
         latest_id = fence->fence_id;
         list_del(&fence->fences);
         glDeleteSync(fence->syncobj);
         free(fence);
      }
      /* don't bother checking any subsequent ones */
      else if (glret == GL_TIMEOUT_EXPIRED) {
         break;
      }
   }

   if (latest_id == 0)
      return;
   clicbs->write_fence(latest_id);
}

static boolean graw_get_one_query_result(GLuint query_id, bool use_64, uint64_t *result)
{
   GLint ready;
   GLuint passed;
   GLuint64 pass64;

   glGetQueryObjectiv(query_id, GL_QUERY_RESULT_AVAILABLE_ARB, &ready);

   if (!ready)
      return FALSE;

   if (use_64) {
      glGetQueryObjectui64v(query_id, GL_QUERY_RESULT_ARB, &pass64);
      *result = pass64;
   } else {
      glGetQueryObjectuiv(query_id, GL_QUERY_RESULT_ARB, &passed);
      *result = passed;
   }
   return TRUE;
}

static boolean graw_check_query(struct grend_query *query)
{
   uint64_t result;
   struct virgl_host_query_state *state;
   struct grend_nontimer_hw_query *hwq, *stor;
   boolean ret;

   if (grend_is_timer_query(query->gltype)) {
       ret = graw_get_one_query_result(query->timer_query_id, TRUE, &result);
       if (ret == FALSE)
           return FALSE;
       goto out_write_val;
   }

   /* for non-timer queries we have to iterate over all hw queries and remove and total them */
   LIST_FOR_EACH_ENTRY_SAFE(hwq, stor, &query->hw_queries, query_list) {
       ret = graw_get_one_query_result(hwq->id, FALSE, &result);
       if (ret == FALSE)
           return FALSE;
       
       /* if this query is done drop it from the list */
       list_del(&hwq->query_list);
       glDeleteQueries(1, &hwq->id);
       FREE(hwq);

       query->current_total += result;
   }
   result = query->current_total;

out_write_val:
   state = query->res->ptr;

   state->result = result;
   state->query_state = VIRGL_QUERY_STATE_DONE;

   query->current_total = 0;
   return TRUE;
}

void graw_renderer_check_queries(void)
{
   struct grend_query *query, *stor;
   if (!inited)
      return;   
   
   LIST_FOR_EACH_ENTRY_SAFE(query, stor, &grend_state.waiting_query_list, waiting_queries) {
      grend_hw_switch_context(vrend_lookup_renderer_ctx(query->ctx_id), TRUE);
      if (graw_check_query(query) == TRUE)
         list_delinit(&query->waiting_queries);
   }
}

static void grend_do_end_query(struct grend_query *q)
{
   glEndQuery(q->gltype);
   q->active_hw = FALSE;
}

static void grend_ctx_finish_queries(struct grend_context *ctx)
{
   struct grend_query *query;

   LIST_FOR_EACH_ENTRY(query, &ctx->active_nontimer_query_list, ctx_queries) {
      if (query->active_hw == TRUE)
         grend_do_end_query(query);
   }
}

static void grend_ctx_restart_queries(struct grend_context *ctx)
{
   struct grend_query *query;
   struct grend_nontimer_hw_query *hwq;

   if (ctx->query_on_hw == TRUE)
      return;

   ctx->query_on_hw = TRUE;
   LIST_FOR_EACH_ENTRY(query, &ctx->active_nontimer_query_list, ctx_queries) {
      if (query->active_hw == FALSE) {
         hwq = grend_create_hw_query(query);
         glBeginQuery(query->gltype, hwq->id);
         query->active_hw = TRUE;
      }
   }
}

/* stop all the nontimer queries running in the current context */
void grend_stop_current_queries(void)
{
   if (grend_state.current_ctx && grend_state.current_ctx->query_on_hw) {
      grend_ctx_finish_queries(grend_state.current_ctx);
      grend_state.current_ctx->query_on_hw = FALSE;
   }
}

boolean grend_hw_switch_context(struct grend_context *ctx, boolean now)
{
   if (ctx == grend_state.current_ctx && ctx->ctx_switch_pending == FALSE)
      return TRUE;

   if (ctx->ctx_id != 0 && ctx->in_error) {
      return FALSE;
   }

   ctx->ctx_switch_pending = TRUE;
   if (now == TRUE) {
      grend_finish_context_switch(ctx);
   }
   grend_state.current_ctx = ctx;
   return TRUE;
}

static void grend_finish_context_switch(struct grend_context *ctx)
{
   if (ctx->ctx_switch_pending == FALSE)
      return;
   ctx->ctx_switch_pending = FALSE;

   if (grend_state.current_hw_ctx == ctx)
      return;

   grend_state.current_hw_ctx = ctx;

   clicbs->make_current(0, ctx->gl_context);

#if 0
   /* re-emit all the state */
   grend_hw_emit_framebuffer_state(ctx);
   grend_hw_emit_depth_range(ctx);
   grend_hw_emit_blend(ctx);
   grend_hw_emit_dsa(ctx);
   grend_hw_emit_rs(ctx);
   grend_hw_emit_blend_color(ctx);
   grend_hw_emit_streamout_targets(ctx);

   ctx->stencil_state_dirty = TRUE;
   ctx->scissor_state_dirty = TRUE;
   ctx->viewport_state_dirty = TRUE;
   ctx->shader_dirty = TRUE;
#endif
}


void
graw_renderer_object_destroy(struct grend_context *ctx, uint32_t handle)
{
   vrend_object_remove(ctx->object_hash, handle, 0);
}

void graw_renderer_object_insert(struct grend_context *ctx, void *data,
                                 uint32_t size, uint32_t handle, enum virgl_object_type type)
{
   vrend_object_insert(ctx->object_hash, data, size, handle, type);
}

static struct grend_nontimer_hw_query *grend_create_hw_query(struct grend_query *query)
{
   struct grend_nontimer_hw_query *hwq;

   hwq = CALLOC_STRUCT(grend_nontimer_hw_query);
   if (!hwq)
      return NULL;

   glGenQueries(1, &hwq->id);
   
   list_add(&hwq->query_list, &query->hw_queries);
   return hwq;
}


void grend_create_query(struct grend_context *ctx, uint32_t handle,
                        uint32_t query_type, uint32_t res_handle,
                        uint32_t offset)
{
   struct grend_query *q;
   struct grend_resource *res;

   res = vrend_resource_lookup(res_handle, ctx->ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   q = CALLOC_STRUCT(grend_query);
   if (!q)
      return;

   list_inithead(&q->waiting_queries);
   list_inithead(&q->ctx_queries);
   list_inithead(&q->hw_queries);
   q->type = query_type;
   q->ctx_id = ctx->ctx_id;

   grend_resource_reference(&q->res, res);

   switch (q->type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
      q->gltype = GL_SAMPLES_PASSED_ARB;      
      break;
   case PIPE_QUERY_OCCLUSION_PREDICATE:
      q->gltype = GL_ANY_SAMPLES_PASSED;
      break;
   case PIPE_QUERY_TIMESTAMP:
      q->gltype = GL_TIMESTAMP;
      break;
   case PIPE_QUERY_TIME_ELAPSED:
      q->gltype = GL_TIME_ELAPSED;
      break;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
      q->gltype = GL_PRIMITIVES_GENERATED;
      break;
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      q->gltype = GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN;
      break;
   default:
      fprintf(stderr,"unknown query object received %d\n", q->type);
      break;
   }

   if (grend_is_timer_query(q->gltype))
      glGenQueries(1, &q->timer_query_id);

   graw_renderer_object_insert(ctx, q, sizeof(struct grend_query), handle,
                               VIRGL_OBJECT_QUERY);
}

static void grend_destroy_query(struct grend_query *query)
{
   struct grend_nontimer_hw_query *hwq, *stor;

   grend_resource_reference(&query->res, NULL);
   list_del(&query->ctx_queries);
   list_del(&query->waiting_queries);
   if (grend_is_timer_query(query->gltype)) {
       glDeleteQueries(1, &query->timer_query_id);
       return;
   }
   LIST_FOR_EACH_ENTRY_SAFE(hwq, stor, &query->hw_queries, query_list) {
      glDeleteQueries(1, &hwq->id);
      FREE(hwq);
   }
   free(query);
}

static void grend_destroy_query_object(void *obj_ptr)
{
   struct grend_query *query = obj_ptr;
   grend_destroy_query(query);
}

void grend_begin_query(struct grend_context *ctx, uint32_t handle)
{
   struct grend_query *q;
   struct grend_nontimer_hw_query *hwq;

   q = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   if (q->gltype == GL_TIMESTAMP)
      return;

   if (grend_is_timer_query(q->gltype)) {
      glBeginQuery(q->gltype, q->timer_query_id);
      return;
   }
   hwq = grend_create_hw_query(q);
   
   /* add to active query list for this context */
   glBeginQuery(q->gltype, hwq->id);

   q->active_hw = TRUE;
   list_addtail(&q->ctx_queries, &ctx->active_nontimer_query_list);
}

void grend_end_query(struct grend_context *ctx, uint32_t handle)
{
   struct grend_query *q;
   q = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   if (grend_is_timer_query(q->gltype)) {
      if (q->gltype == GL_TIMESTAMP)
         glQueryCounter(q->timer_query_id, q->gltype);
         /* remove from active query list for this context */
      else
         glEndQuery(q->gltype);
      return;
   }

   if (q->active_hw)
      grend_do_end_query(q);

   list_delinit(&q->ctx_queries);
}

void grend_get_query_result(struct grend_context *ctx, uint32_t handle,
                            uint32_t wait)
{
   struct grend_query *q;
   boolean ret;

   q = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   ret = graw_check_query(q);
   if (ret == FALSE)
      list_addtail(&q->waiting_queries, &grend_state.waiting_query_list);
}

void grend_render_condition(struct grend_context *ctx,
                            uint32_t handle,
                            boolean condtion,
                            uint mode)
{
   struct grend_query *q;
   GLenum glmode;
   struct grend_nontimer_hw_query *hwq, *last;

   if (handle == 0) {
      glEndConditionalRenderNV();
      return;
   }

   q = vrend_object_lookup(ctx->object_hash, handle, VIRGL_OBJECT_QUERY);
   if (!q)
      return;

   switch (mode) {
   case PIPE_RENDER_COND_WAIT:
      glmode = GL_QUERY_WAIT;
      break;
   case PIPE_RENDER_COND_NO_WAIT:
      glmode = GL_QUERY_NO_WAIT;
      break;
   case PIPE_RENDER_COND_BY_REGION_WAIT:
      glmode = GL_QUERY_BY_REGION_WAIT;
      break;
   case PIPE_RENDER_COND_BY_REGION_NO_WAIT:
      glmode = GL_QUERY_BY_REGION_NO_WAIT;
      break;
   }

   LIST_FOR_EACH_ENTRY(hwq, &q->hw_queries, query_list)
      last = hwq;
      
   glBeginConditionalRender(last->id, glmode);
   
}

void grend_set_query_state(struct grend_context *ctx,
                           boolean enabled)
{

}

void grend_set_cursor_info(uint32_t cursor_handle, int x, int y)
{
   grend_state.cursor_info.res_handle = cursor_handle;
   grend_state.cursor_info.x = x;
   grend_state.cursor_info.y = y;

//   if (frontbuffer && draw_cursor)
//      graw_renderer_remove_cursor(&grend_state.cursor_info, frontbuffer);
}

void grend_create_so_target(struct grend_context *ctx,
                            uint32_t handle,
                            uint32_t res_handle,
                            uint32_t buffer_offset,
                            uint32_t buffer_size)
{
   struct grend_so_target *target;
   struct grend_resource *res;

   res = vrend_resource_lookup(res_handle, ctx->ctx_id);
   if (!res) {
      report_context_error(ctx, VIRGL_ERROR_CTX_ILLEGAL_RESOURCE, res_handle);
      return;
   }

   target = CALLOC_STRUCT(grend_so_target);
   if (!target)
      return;

   pipe_reference_init(&target->reference, 1);
   target->res_handle = res_handle;
   target->buffer_offset = buffer_offset;
   target->buffer_size = buffer_size;

   grend_resource_reference(&target->buffer, res);

   vrend_object_insert(ctx->object_hash, target, sizeof(*target), handle,
                       VIRGL_OBJECT_STREAMOUT_TARGET);
}

static void vrender_get_glsl_version(int *major, int *minor)
{
   int major_local, minor_local;
   const GLubyte *version_str;
   int c;

   version_str = glGetString(GL_SHADING_LANGUAGE_VERSION);
   c = sscanf((const char *)version_str, "%i.%i",
              &major_local, &minor_local);
   assert(c == 2);

   if (major)
      *major = major_local;
   if (minor)
      *minor = minor_local;
}

void graw_renderer_fill_caps(uint32_t set, uint32_t version,
                             union virgl_caps *caps)
{
   int i;
   GLint max;
   int glsl_major, glsl_minor;
   int gl_ver = epoxy_gl_version();
   memset(caps, 0, sizeof(*caps));

   if (set != 0) {
      caps->max_version = 0;
      return;
   }
   vrender_get_glsl_version(&glsl_major, &glsl_minor);
   caps->max_version = 1;

   caps->v1.bset.occlusion_query = 1;
   if (gl_ver >= 30) {
      caps->v1.bset.indep_blend_enable = 1;
      caps->v1.bset.conditional_render = 1;
   } else {
      if (glewIsSupported("GL_EXT_draw_buffers2"))
         caps->v1.bset.indep_blend_enable = 1;
      if (glewIsSupported("GL_NV_conditional_render"))
         caps->v1.bset.conditional_render = 1;
   }

   if (gl_ver >= 31) {
      caps->v1.bset.instanceid = 1;
   } else {
      if (glewIsSupported("GL_ARB_draw_instanced"))
         caps->v1.bset.instanceid = 1;
   }

   if (grend_state.have_nv_prim_restart || grend_state.have_gl_prim_restart)
      caps->v1.bset.primitive_restart = 1;

   if (gl_ver >= 32) {
      caps->v1.bset.fragment_coord_conventions = 1;
   } else {
      if (glewIsSupported("GL_ARB_fragment_coord_conventions"))
         caps->v1.bset.fragment_coord_conventions = 1;
   }

   if (glewIsSupported("GL_ARB_texture_multisample")) {
       /* disable multisample until developed */
      caps->v1.bset.texture_multisample = 1;
   }
   if (gl_ver >= 40) {
      caps->v1.bset.indep_blend_func = 1;
      caps->v1.bset.cube_map_array = 1;
   } else {
      if (glewIsSupported("GL_ARB_draw_buffers_blend"))
         caps->v1.bset.indep_blend_func = 1;
      if (glewIsSupported("GL_ARB_texture_cube_map_array"))
         caps->v1.bset.cube_map_array = 1;
   }

   if (gl_ver >= 42) {
      caps->v1.bset.start_instance = 1;
   } else {
      if (glewIsSupported("GL_ARB_base_instance"))      
         caps->v1.bset.start_instance = 1;
   }         
   if (glewIsSupported("GL_ARB_shader_stencil_export"))
      caps->v1.bset.shader_stencil_export = 1;

   caps->v1.glsl_level = 130;
   if (glewIsSupported("GL_EXT_texture_array"))
      caps->v1.max_texture_array_layers = 256;
   caps->v1.max_streamout_buffers = 4;
   if (glewIsSupported("GL_ARB_blend_func_extended")) {
      glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &max);
      caps->v1.max_dual_source_render_targets = max;
   } else
      caps->v1.max_dual_source_render_targets = 0;

   glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max);
   caps->v1.max_render_targets = max;

   glGetIntegerv(GL_MAX_SAMPLES, &max);
   caps->v1.max_samples = max;

   for (i = 0; i < VIRGL_FORMAT_MAX; i++) {
      uint32_t offset = i / 32;
      uint32_t index = i % 32;

      if (tex_conv_table[i].internalformat != 0) {
         caps->v1.sampler.bitmask[offset] |= (1 << index);
         if (vrend_format_can_render(i))
            caps->v1.render.bitmask[offset] |= (1 << index);
      }
   }

}

GLint64 graw_renderer_get_timestamp(void)
{
   GLint64 v;
   glGetInteger64v(GL_TIMESTAMP, &v);
   return v;
}

void *graw_renderer_get_cursor_contents(uint32_t res_handle, uint32_t *width, uint32_t *height)
{
   GLenum format, type;
   struct grend_resource *res;
   int blsize;
   void *data, *data2;
   int size;
   int h;

   res = vrend_resource_lookup(res_handle, 0);
   if (!res)
      return NULL;

   if (res->base.width0 > 128 || res->base.height0 > 128)
      return NULL;

   if (res->target != GL_TEXTURE_2D)
      return NULL;

   *width = res->base.width0;
   *height = res->base.height0;
   format = tex_conv_table[res->base.format].glformat;
   type = tex_conv_table[res->base.format].gltype; 
   blsize = util_format_get_blocksize(res->base.format);
   size = util_format_get_nblocks(res->base.format, res->base.width0, res->base.height0) * blsize;
   data = malloc(size);
   data2 = malloc(size);

   glBindTexture(res->target, res->id);
   glGetnTexImageARB(res->target, 0, format, type, size, data);

   for (h = 0; h < res->base.height0; h++) {
      uint32_t doff = (res->base.height0 - h - 1) * res->base.width0 * blsize;
      uint32_t soff = h * res->base.width0 * blsize;

      memcpy(data2 + doff, data + soff, res->base.width0 * blsize);
   }
   free(data);
      
   return data2;
}

void graw_renderer_force_ctx_0(void)
{
   grend_state.current_ctx = NULL;
   grend_state.current_hw_ctx = NULL;
   grend_hw_switch_context(vrend_lookup_renderer_ctx(0), TRUE);
}

void graw_renderer_get_rect(int idx, struct virgl_iovec *iov, unsigned int num_iovs,
                            uint32_t offset, int x, int y, int width, int height)
{
   struct grend_resource *res = frontbuffer[idx];
   struct pipe_box box;
   int elsize = util_format_get_blocksize(res->base.format);
   int stride;
   box.x = x;
   box.y = y;
   box.z = 0;
   box.width = width;
   box.height = height;
   box.depth = 1;

   stride = util_format_get_nblocksx(res->base.format, res->base.width0) * elsize;
   graw_renderer_transfer_send_iov(res->handle, 0,
                                   0, stride, 0, &box, offset, iov, num_iovs);
}
                                   
void graw_renderer_dump_resource(void *data)
{
   struct grend_resource *res = data;

   fprintf(stderr, "target %d, width, height %dx%d\n", res->target, res->base.width0, res->base.height0);

}
