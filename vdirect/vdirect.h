/**************************************************************************
 *
 * Copyright (C) 2015 Red Hat Inc.
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

#ifndef VTEST_H
#define VTEST_H

#include <errno.h>

struct vdirect_context;

struct vdirect_buffer {
   const char *buffer;
   int size;
};

struct vdirect_input {
   union {
      int fd;
      struct vdirect_buffer *buffer;
   } data;
   int (*read)(struct vdirect_input *input, void *buf, int size);
};
#include "util/u_double_list.h"


struct vdirect_renderer {
   uint32_t max_length;

   int fence_id;
   int last_fence;

   struct list_head active_contexts;
   struct list_head free_contexts;
   int next_context_id;

   struct vdirect_context *current_context;
   struct vdirect_egl *egl;
};


int vdirect_init_renderer(int ctx_flags, const char *render_device);
void vdirect_cleanup_renderer(void);

int vdirect_create_context(struct vdirect_input *input, int out_fd,
                         uint32_t length_dw, struct vdirect_context **out_ctx);
int vdirect_lazy_init_context(struct vdirect_context *ctx);
void vdirect_destroy_context(struct vdirect_context *ctx);

void vdirect_set_current_context(struct vdirect_context *ctx);

int vdirect_send_caps(uint32_t length_dw);
int vdirect_send_caps2(uint32_t length_dw);
int vdirect_create_resource(uint32_t length_dw);
int vdirect_create_resource2(uint32_t length_dw);
int vdirect_resource_unref(uint32_t length_dw);
int vdirect_submit_cmd(uint32_t length_dw);

int vdirect_transfer_get(uint32_t length_dw);
int vdirect_transfer_get_nop(uint32_t length_dw);
int vdirect_transfer_get2(uint32_t length_dw);
int vdirect_transfer_get2_nop(uint32_t length_dw);
int vdirect_transfer_put(uint32_t length_dw);
int vdirect_transfer_put_nop(uint32_t length_dw);
int vdirect_transfer_put2(uint32_t length_dw);
int vdirect_transfer_put2_nop(uint32_t length_dw);

int vdirect_block_read(struct vdirect_input *input, void *buf, int size);
int vdirect_buf_read(struct vdirect_input *input, void *buf, int size);

int vdirect_resource_busy_wait(uint32_t length_dw);
int vdirect_renderer_create_fence(void);
int vdirect_poll(void);

int vdirect_ping_protocol_version(uint32_t length_dw);
int vdirect_protocol_version(uint32_t length_dw);

/* since protocol version 3 */
int vdirect_get_param(uint32_t length_dw);
int vdirect_get_capset(uint32_t length_dw);
int vdirect_context_init(uint32_t length_dw);

void vdirect_set_max_length(uint32_t length);

#endif

