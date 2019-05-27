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

#include <string.h>
#include <stdlib.h>
#include "vrend_tweaks.h"
#include "vrend_debug.h"
#include "virgl_protocol.h"

inline static void get_tf3_samples_passed_factor(struct vrend_context_tweaks *ctx, void *params)
{
   *(uint32_t *)params =  ctx->tf3_samples_passed_factor;
}

bool vrend_get_tweak_is_active_with_params(struct vrend_context_tweaks *ctx, enum vrend_tweak_type t, void *params)
{
   if (!(ctx->active_tweaks & (1u << t)))
      return false;

   return true;
}

bool vrend_get_tweak_is_active(struct vrend_context_tweaks *ctx, enum vrend_tweak_type t)
{
   return (ctx->active_tweaks & (1u << t)) ? true : false;
}

const char *tweak_debug_table[] = {
   [virgl_tweak_undefined] = "Undefined tweak"
};

static void set_tweak_and_params(struct vrend_context_tweaks *ctx,
                                 enum vrend_tweak_type t, uint32_t value)
{
   ctx->active_tweaks |= 1u << t;
}

static void set_tweak_and_params_from_string(struct vrend_context_tweaks *ctx,
                                             enum vrend_tweak_type t, const char *value)
{
   ctx->active_tweaks |= 1u << t;
   (void)value;
}

/* we expect a string like tweak1:value,tweak2:value */
void vrend_set_active_tweaks(struct vrend_context_tweaks *ctx, uint32_t tweak_id, uint32_t value)
{
   if (tweak_id < virgl_tweak_undefined) {
      VREND_DEBUG(dbg_tweak, NULL, "Apply tweak '%s' = %u\n", tweak_debug_table[tweak_id], value);
      set_tweak_and_params(ctx, tweak_id, value);
   } else {
      VREND_DEBUG(dbg_tweak, NULL, "Unknown tweak %d = %d sent\n", tweak_id, value);
   }
}
