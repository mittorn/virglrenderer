#ifndef GRAW_CURSOR_H
#define GRAW_CURSOR_H

/* cursor drawing state */

struct graw_cursor_info {
   GLuint prog_id;
   GLuint vaoid;
   GLuint vbo_id;
   uint32_t res_handle;
   int x, y;
   GLuint attrib_locs[2];
   GLuint samp_loc;

   int last_x, last_y;
};

void graw_cursor_init(struct graw_cursor_info *cursor);

int graw_renderer_paint_cursor(struct graw_cursor_info *cursor,
                               struct grend_resource *dst_res);
int graw_renderer_remove_cursor(struct graw_cursor_info *cursor,
                                struct grend_resource *dst_res);
#endif
