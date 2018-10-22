/**************************************************************************
 *
 * Copyright (C) 2018 Collabora Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef vrend_debug_h
#define vrend_debug_h

struct vrend_context;
void vrend_print_context_name(struct vrend_context *ctx);

enum virgl_debug_flags {
   dbg_shader_tgsi = 1 << 0,
   dbg_shader_glsl = 1 << 1,
   dbg_shader_streamout = 1 << 2,
   dbg_shader = dbg_shader_tgsi | dbg_shader_glsl | dbg_shader_streamout,
};

void vrend_init_debug_flags(void);

unsigned vrend_debug(struct vrend_context *ctx, enum virgl_debug_flags flag);

#ifndef NDEBUG
#define VREND_DEBUG(flag, ctx,  ...) \
   if (vrend_debug(ctx, flag)) \
      do { \
            vrend_print_context_name(ctx); \
            fprintf(stderr, __VA_ARGS__); \
      } while (0)

#define VREND_DEBUG_EXT(flag, ctx, X) \
   if (vrend_debug(ctx, flag)) \
      do { \
            vrend_print_context_name(ctx); \
            X; \
      } while (0)

#else
#define VREND_DEBUG(flag, ctx, fmt, ...)
#define VREND_DEBUG_EXT(flag, ctx, X)
#endif

#endif
