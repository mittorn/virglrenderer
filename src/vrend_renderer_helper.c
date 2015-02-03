/**************************************************************************
 *
 * Copyright (C) 2014 Red Hat Inc.
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
/* helper library for qemu local renderers like SDL / GTK
   flushes the given texture to the frontbuffer */
#include <epoxy/gl.h>
#include "virgl_helper.h"

#define MAX_HELPER_SCANOUT 4

static struct helper_scanout {
    GLuint tex_id;
    GLuint fb_id;
    int x, y;
    uint32_t width, height;
    unsigned flags;
} frontbuf[MAX_HELPER_SCANOUT];

void virgl_helper_scanout_info(int idx,
                               uint32_t tex_id,
                               uint32_t flags,
                               int x, int y,
                               uint32_t width, uint32_t height)
{

    frontbuf[idx].x = x;
    frontbuf[idx].y = y;
    frontbuf[idx].width = width;
    frontbuf[idx].height = height;
    frontbuf[idx].tex_id = tex_id;
    frontbuf[idx].flags = flags;
    if (tex_id == 0 && width == 0 && height == 0) {
       if (frontbuf[idx].fb_id) {
          glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                    GL_TEXTURE_2D, 0, 0);
          glDeleteFramebuffers(1, &frontbuf[idx].fb_id);
          frontbuf[idx].fb_id = 0;
       }
       return;
    }
    if (!frontbuf[idx].fb_id)
        glGenFramebuffers(1, &frontbuf[idx].fb_id);

    glBindFramebuffer(GL_FRAMEBUFFER_EXT, frontbuf[idx].fb_id);

    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                              GL_TEXTURE_2D, tex_id, 0);
}

void virgl_helper_flush_scanout(int idx,
                                int x, int y,
                                uint32_t width, uint32_t height)
{
    uint32_t sy1, sy2, dy1, dy2;

    if (!frontbuf[idx].width || !frontbuf[idx].height)
       return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, frontbuf[idx].fb_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    dy1 = frontbuf[idx].height - y - frontbuf[idx].y;
    dy2 = frontbuf[idx].height - y - height - frontbuf[idx].y;
    if (frontbuf[idx].flags & VIRGL_HELPER_Y_0_TOP) {
        sy1 = frontbuf[idx].height - y;
        sy2 = frontbuf[idx].height - y - height;
    } else {
        sy1 = y;
        sy2 = y + height;
    }

    glViewport(0, 0, frontbuf[idx].width, frontbuf[idx].height);
    glBlitFramebuffer(x, sy1, x + width, sy2,
                      x - frontbuf[idx].x, dy1, x + width - frontbuf[idx].x, dy2,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
}
