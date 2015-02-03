/* helper functions for testing purposes */
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "testvirgl.h"

#include "virglrenderer.h"

void testvirgl_init_simple_1d_resource(struct virgl_renderer_resource_create_args *res, int handle)
{
    res->handle = handle;
    res->target = PIPE_TEXTURE_1D;
    res->format = PIPE_FORMAT_B8G8R8X8_UNORM;
    res->width = 50;
    res->height = 50;
    res->depth = 1;
    res->array_size = 1;
    res->last_level = 0;
    res->nr_samples = 0;
    res->bind = PIPE_BIND_SAMPLER_VIEW;
    res->flags = 0;
}
