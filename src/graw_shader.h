#ifndef GRAW_SHADER_H
#define GRAW_SHADER_H

#include "pipe/p_state.h"

#define SHADER_FLAG_FS_INVERT 1

/* need to store patching info for interpolation */
struct vrend_interp_info {
   int semantic_name;
   int semantic_index;
   int interpolate;
};

struct vrend_shader_info {
   uint32_t samplers_used_mask;
   int num_consts;
   int num_inputs;
   int num_interps;
   int num_outputs;
   uint32_t shadow_samp_mask;
   struct pipe_stream_output_info so_info;

   struct vrend_interp_info *interpinfo;
};

struct vrend_shader_key {
   uint32_t coord_replace;
   boolean invert_fs_origin;
};
      
boolean vrend_patch_vertex_shader_interpolants(char *program,
                                               struct vrend_shader_info *vs_info,
                                               struct vrend_shader_info *fs_info);

char *tgsi_convert(const struct tgsi_token *tokens,
                   struct vrend_shader_key *key,
                   struct vrend_shader_info *sinfo);

#endif
