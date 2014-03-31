#include <epoxy/gl.h>
#include <stdio.h>
#include "graw_renderer.h"
#include "vrend_object.h"
#include "graw_cursor.h"

static const GLchar *cursor_vs_shader = 
   "attribute vec2 position;\n"
   "attribute vec2 textureCoords;\n"
   "varying vec2 texCoords;\n"
   "void main()\n"
   "{\n"
   "   texCoords = textureCoords;\n"
   "   gl_Position = vec4(position, 0.0, 1.0);\n"
   "}\n";

static const GLchar *cursor_fs_shader =
   "uniform sampler2D texSampler;\n"
   "varying vec2 texCoords;\n"
   "void main()\n"
   "{\n"
   "   gl_FragColor = texture2D(texSampler, texCoords);\n"
   "}\n";

void graw_cursor_init(struct graw_cursor_info *cursor)
{
   GLuint curs_vs_id, curs_fs_id;

   cursor->prog_id = glCreateProgram();
   curs_vs_id = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(curs_vs_id, 1, &cursor_vs_shader, NULL);
   glCompileShader(curs_vs_id);

   curs_fs_id = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(curs_fs_id, 1, &cursor_fs_shader, NULL);
   glCompileShader(curs_fs_id);

   glAttachShader(cursor->prog_id, curs_vs_id);
   glAttachShader(cursor->prog_id, curs_fs_id);
   glLinkProgram(cursor->prog_id);

   glGenBuffersARB(1, &cursor->vbo_id);
   glBindBufferARB(GL_ARRAY_BUFFER_ARB, cursor->vbo_id);
   glBufferData(GL_ARRAY_BUFFER_ARB, 4 * 4 * sizeof(GLfloat), NULL, GL_STREAM_DRAW);

   cursor->attrib_locs[0] = glGetAttribLocation(cursor->prog_id, "position");
   cursor->attrib_locs[1] = glGetAttribLocation(cursor->prog_id, "textureCoords");
   cursor->samp_loc = glGetUniformLocation(cursor->prog_id, "texSampler");

   glGenVertexArrays(1, &cursor->vaoid);

   grend_bind_va(cursor->vaoid);

   glVertexAttribPointer(cursor->attrib_locs[0], 2, GL_FLOAT, GL_FALSE, 16, 0);
   glVertexAttribDivisorARB(cursor->attrib_locs[0], 0);
   glVertexAttribPointer(cursor->attrib_locs[1], 2, GL_FLOAT, GL_FALSE, 16, (GLvoid *)8);
   glVertexAttribDivisorARB(cursor->attrib_locs[1], 0);
   
   glEnableVertexAttribArray(cursor->attrib_locs[0]);
   glEnableVertexAttribArray(cursor->attrib_locs[1]);

}

int graw_renderer_remove_cursor(struct graw_cursor_info *cursor,
                                struct grend_resource *dst_res)
{
   struct pipe_box box;
   box.x = cursor->last_x;
   box.y = cursor->last_y;
   box.z = 0;
   box.width = 64;
   box.height = 64;
   box.depth = 1;

   graw_renderer_flush_buffer_res(dst_res, &box);
   return 0;
}

int graw_renderer_paint_cursor(struct graw_cursor_info *cursor,
                               struct grend_resource *dst_res)
{
   GLuint fb_id;
   struct grend_resource *cursor_res;
   struct vertex {
      GLfloat x, y, s, t;
   };
   struct vertex verts[4];
   GLuint locs[2];
   GLfloat x0, y0, x1, y1;
   int s_w, s_h;
   if (!cursor->res_handle)
      return 0;

   cursor_res = vrend_resource_lookup(cursor->res_handle, 0);
   if (!cursor_res)
      return 0;

   s_w = dst_res->base.width0;
   s_h = dst_res->base.height0;

   glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
   glDrawBuffer(GL_BACK);

   grend_use_program(cursor->prog_id);

   glUniform1i(cursor->samp_loc, 0);

   grend_blend_enable(GL_TRUE);
   grend_depth_test_enable(GL_FALSE);

   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glBlendEquation(GL_FUNC_ADD);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(cursor_res->target, cursor_res->id);

   glTexParameteri(cursor_res->target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(cursor_res->target, GL_TEXTURE_MAX_LEVEL, 0);
   glTexParameterf(cursor_res->target, GL_TEXTURE_MIN_LOD, 0);
   glTexParameterf(cursor_res->target, GL_TEXTURE_MAX_LOD, 0);
   glTexParameterf(cursor_res->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameterf(cursor_res->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   grend_bind_va(cursor->vaoid);

   glBindBufferARB(GL_ARRAY_BUFFER_ARB, cursor->vbo_id);

   cursor->last_x = cursor->x;
   cursor->last_y = cursor->y;

   x0 = ((float)cursor->x / (s_w / 2)) - 1.0;
   y0 = ((float)(s_h - cursor->y - cursor_res->base.width0) / (s_h / 2)) - 1.0;
   x1 = (((float)cursor->x + cursor_res->base.height0) / (s_w / 2)) - 1.0;
   y1 = (((float)(s_h - cursor->y)) / (s_h / 2)) - 1.0;

   verts[0].x = x0;
   verts[0].y = y0;

   verts[1].x = x1;
   verts[1].y = y0;

   verts[2].x = x1;
   verts[2].y = y1;

   verts[3].x = x0;
   verts[3].y = y1;

   verts[0].s = 0.0;
   verts[0].t = 1.0;
   verts[1].s = 1.0;
   verts[1].t = 1.0;
   verts[2].s = 1.0;
   verts[2].t = 0.0;
   verts[3].s = 0.0;
   verts[3].t = 0.0;
 
   glBufferSubData(GL_ARRAY_BUFFER_ARB, 0, sizeof(verts), verts);

   glDrawArrays(GL_QUADS, 0, 4);
   return 0;
}
