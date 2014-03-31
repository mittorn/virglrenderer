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
