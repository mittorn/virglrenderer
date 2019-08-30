/**************************************************************************
 *
 * Copyright (C) 2020 Chromium
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

#ifndef VIRGL_RESOURCE_H
#define VIRGL_RESOURCE_H

#include <stdint.h>

struct iovec;
struct pipe_resource;

/**
 * A global cross-context resource.  A virgl_resource is not directly usable
 * by renderer contexts, but must be attached and imported into renderer
 * contexts to create context objects first.  For example, it can be attached
 * and imported into a vrend_decode_ctx to create a vrend_resource.
 *
 * It is also possible to create a virgl_resource from a context object.
 */
struct virgl_resource {
   uint32_t res_id;

   const struct iovec *iov;
   int iov_count;

   void *private_data;

   struct pipe_resource *pipe_resource;
};

struct virgl_resource_pipe_callbacks {
   void *data;

   void (*unref)(struct pipe_resource *pres, void *data);

   void (*attach_iov)(struct pipe_resource *pres,
                      const struct iovec *iov,
                      int iov_count,
                      void *data);
   void (*detach_iov)(struct pipe_resource *pres, void *data);
};

int
virgl_resource_table_init(const struct virgl_resource_pipe_callbacks *callbacks);

void
virgl_resource_table_cleanup(void);

void
virgl_resource_table_reset(void);

int
virgl_resource_create_from_pipe(uint32_t res_id, struct pipe_resource *pres);

int
virgl_resource_create_from_iov(uint32_t res_id,
                               const struct iovec *iov,
                               int iov_count);

void
virgl_resource_remove(uint32_t res_id);

struct virgl_resource *
virgl_resource_lookup(uint32_t res_id);

int
virgl_resource_attach_iov(struct virgl_resource *res,
                          const struct iovec *iov,
                          int iov_count);

void
virgl_resource_detach_iov(struct virgl_resource *res);

#endif /* VIRGL_RESOURCE_H */
