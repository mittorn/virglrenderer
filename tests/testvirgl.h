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

#ifndef TESTVIRGL_H
#define TESTVIRGL_H

#include "virglrenderer.h"
#include "pipe/p_state.h"

#define VIRGL_MAX_CMDBUF_DWORDS (16*1024)

struct virgl_cmd_buf {
    unsigned cdw;
    uint32_t *buf;
};

struct virgl_context {
    struct virgl_cmd_buf *cbuf;
};

struct virgl_so_target {
    uint32_t handle;
};
struct virgl_sampler_view {
    uint32_t handle;
};

struct virgl_resource {
    struct pipe_resource base;
    uint32_t handle;
};


void testvirgl_init_simple_buffer(struct virgl_renderer_resource_create_args *res, int handle);
void testvirgl_init_simple_1d_resource(struct virgl_renderer_resource_create_args *args, int handle);
void testvirgl_init_simple_2d_resource(struct virgl_renderer_resource_create_args *res, int handle);
int testvirgl_init_single_ctx(void);
void testvirgl_init_single_ctx_nr(void);
void testvirgl_fini_single_ctx(void);

#endif
