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
//int vtest_create_renderer(int in_fd, int out_fd, uint32_t length);
//int vtest_wait_for_fd_read(int fd);

//int vtest_send_caps(void);
//int vtest_send_caps2(void);
//int vtest_create_resource(void);
//int vtest_resource_unref(void);
//int vtest_submit_cmd(uint32_t length_dw);

//int vtest_transfer_get(uint32_t length_dw);
//int vtest_transfer_put(uint32_t length_dw);

//int vtest_block_read(int fd, void *buf, int size);

//int vtest_resource_busy_wait(void);
//int vtest_renderer_create_fence(void);
//int vtest_poll(void);
//int vtest_flush_frontbuffer(void);

//void vtest_destroy_renderer(void);
int run_renderer(int in_fd, int ctx_id);
int wait_for_socket_accept(int sock);
int vtest_open_socket(const char *path);
int renderer_loop( void *d);
#endif

