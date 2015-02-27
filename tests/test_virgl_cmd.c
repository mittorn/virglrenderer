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
#include <check.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <virglrenderer.h>
#include "virgl_hw.h"
#include "pipe/p_format.h"
#include "testvirgl_encode.h"

/* create a resource - clear it to a color, do a transfer */
START_TEST(virgl_test_clear)
{
    struct virgl_context ctx;
    struct virgl_resource res;
    struct virgl_surface surf;
    struct pipe_framebuffer_state fb_state;
    union pipe_color_union color;
    struct virgl_box box;
    int ret;
    int i;
    
    ret = testvirgl_init_ctx_cmdbuf(&ctx);
    ck_assert_int_eq(ret, 0);    

    /* init and create simple 2D resource */
    ret = testvirgl_create_backed_simple_2d_res(&res, 1, 50, 50);
    ck_assert_int_eq(ret, 0);
    
    /* attach resource to context */
    virgl_renderer_ctx_attach_resource(ctx.ctx_id, res.handle);

    /* create a surface for the resource */
    memset(&surf, 0, sizeof(surf));
    surf.base.format = PIPE_FORMAT_B8G8R8X8_UNORM;
    surf.handle = 1;
    surf.base.texture = &res.base;

    virgl_encoder_create_surface(&ctx, surf.handle, &res, &surf.base);

    /* set the framebuffer state */
    fb_state.nr_cbufs = 1;
    fb_state.zsbuf = NULL;
    fb_state.cbufs[0] = &surf.base;
    virgl_encoder_set_framebuffer_state(&ctx, &fb_state);

    /* clear the resource */
    /* clear buffer to green */
    color.f[0] = 0.0;
    color.f[1] = 1.0;
    color.f[2] = 0.0;
    color.f[3] = 1.0;
    virgl_encode_clear(&ctx, PIPE_CLEAR_COLOR0, &color, 0.0, 0);

    /* submit the cmd stream */
    virgl_renderer_submit_cmd(ctx.cbuf->buf, ctx.ctx_id, ctx.cbuf->cdw);

    /* read back the cleared values in the resource */
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = 5;
    box.h = 1;
    box.d = 1;
    ret = virgl_renderer_transfer_read_iov(res.handle, ctx.ctx_id, 0, 50, 0, &box, 0, NULL, 0);
    ck_assert_int_eq(ret, 0);

    /* check the returned values */
    for (i = 0; i < 5; i++) {
	uint32_t *ptr = res.iovs[0].iov_base;
	ck_assert_int_eq(ptr[i], 0xff00ff00);
    }

    /* cleanup */
    virgl_renderer_ctx_detach_resource(ctx.ctx_id, res.handle);

    testvirgl_destroy_backed_res(&res);

    testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST 

START_TEST(virgl_test_blit_simple)
{
    struct virgl_context ctx;
    struct virgl_resource res, res2;
    struct virgl_surface surf;
    struct pipe_framebuffer_state fb_state;
    union pipe_color_union color;
    struct pipe_blit_info blit;
    struct virgl_box box;
    int ret;
    int i;

    ret = testvirgl_init_ctx_cmdbuf(&ctx);
    ck_assert_int_eq(ret, 0);

    /* init and create simple 2D resource */
    ret = testvirgl_create_backed_simple_2d_res(&res, 1, 50, 50);
    ck_assert_int_eq(ret, 0);

    /* init and create simple 2D resource */
    ret = testvirgl_create_backed_simple_2d_res(&res2, 2, 50, 50);
    ck_assert_int_eq(ret, 0);

    /* attach resource to context */
    virgl_renderer_ctx_attach_resource(ctx.ctx_id, res.handle);
    virgl_renderer_ctx_attach_resource(ctx.ctx_id, res2.handle);

        /* create a surface for the resource */
    memset(&surf, 0, sizeof(surf));
    surf.base.format = PIPE_FORMAT_B8G8R8X8_UNORM;
    surf.handle = 1;
    surf.base.texture = &res.base;

    virgl_encoder_create_surface(&ctx, surf.handle, &res, &surf.base);

    /* set the framebuffer state */
    fb_state.nr_cbufs = 1;
    fb_state.zsbuf = NULL;
    fb_state.cbufs[0] = &surf.base;
    virgl_encoder_set_framebuffer_state(&ctx, &fb_state);

    /* clear the resource */
    /* clear buffer to green */
    color.f[0] = 0.0;
    color.f[1] = 1.0;
    color.f[2] = 0.0;
    color.f[3] = 1.0;
    virgl_encode_clear(&ctx, PIPE_CLEAR_COLOR0, &color, 0.0, 0);

    memset(&blit, 0, sizeof(blit));
    blit.mask = PIPE_MASK_RGBA;
    blit.dst.format = res2.base.format;
    blit.dst.box.width = 10;
    blit.dst.box.height = 1;
    blit.dst.box.depth = 1;
    blit.src.format = res.base.format;
    blit.src.box.width = 10;
    blit.src.box.height = 1;
    blit.src.box.depth = 1;
    virgl_encode_blit(&ctx, &res2, &res, &blit);

    /* submit the cmd stream */
    virgl_renderer_submit_cmd(ctx.cbuf->buf, ctx.ctx_id, ctx.cbuf->cdw);

    /* read back the cleared values in the resource */
    box.x = 0;
    box.y = 0;
    box.z = 0;
    box.w = 5;
    box.h = 1;
    box.d = 1;
    ret = virgl_renderer_transfer_read_iov(res2.handle, ctx.ctx_id, 0, 50, 0, &box, 0, NULL, 0);
    ck_assert_int_eq(ret, 0);

    /* check the returned values */
    for (i = 0; i < 5; i++) {
	uint32_t *ptr = res2.iovs[0].iov_base;
	ck_assert_int_eq(ptr[i], 0xff00ff00);
    }

    /* cleanup */
    virgl_renderer_ctx_detach_resource(ctx.ctx_id, res2.handle);
    virgl_renderer_ctx_detach_resource(ctx.ctx_id, res.handle);

    testvirgl_destroy_backed_res(&res);
    testvirgl_destroy_backed_res(&res2);

    testvirgl_fini_ctx_cmdbuf(&ctx);
}
END_TEST

Suite *virgl_init_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("virgl_clear");
  tc_core = tcase_create("clear");

  tcase_add_test(tc_core, virgl_test_clear);
  tcase_add_test(tc_core, virgl_test_blit_simple);
  suite_add_tcase(s, tc_core);
  return s;

}

int main(void)
{
  Suite *s;
  SRunner *sr;
  int number_failed;

  s = virgl_init_suite();
  sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return number_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
