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

#include "vrend_debug.h"
#include "vrend_renderer.h"
#include "util/u_debug.h"
#include <stdlib.h>
#include <stdio.h>
static const struct debug_named_value vrend_debug_options[] = {
   {"tgsi", dbg_shader_tgsi, "Print TGSI"},
   {"glsl", dbg_shader_glsl, "Print GLSL shaders created from TGSI"},
   {"shader", dbg_shader, "Print TGSI and created GLSL shaders"},
   {"stream", dbg_shader_streamout, "Print shader streamout"},
   DEBUG_NAMED_VALUE_END
};

static uint64_t vrend_debug_flags = 0;
static int vrend_debug_flags_initalized = 0;
void vrend_init_debug_flags()
{
   if (!vrend_debug_flags_initalized)  {
      vrend_debug_flags_initalized = 1;
      vrend_debug_flags = debug_get_flags_option("VREND_DEBUG",
                                                 vrend_debug_options, 0);
   }
}

unsigned vrend_debug(struct vrend_context *ctx, enum virgl_debug_flags flag)
{
   return (vrend_debug_flags & flag) || vrend_context_has_debug_flag(ctx, flag);
}
