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

/* transfer and iov related tests */
#include <check.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <errno.h>
#include <virglrenderer.h>
#include "pipe/p_defines.h"
#include "virgl_hw.h"
#include "testvirgl.h"

/* pass an illegal context to transfer fn */
START_TEST(virgl_test_transfer_read_illegal_ctx)
{
  int ret;
  struct virgl_box box;

  ret = virgl_renderer_transfer_read_iov(1, 2, 0, 1, 1, &box, 0, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);
}
END_TEST

START_TEST(virgl_test_transfer_write_illegal_ctx)
{
  int ret;
  struct virgl_box box;

  ret = virgl_renderer_transfer_write_iov(1, 2, 0, 1, 1, &box, 0, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);
}
END_TEST

/* pass a resource not bound to the context to transfers */
START_TEST(virgl_test_transfer_read_unbound_res)
{
  int ret;
  struct virgl_box box;

  ret = virgl_renderer_transfer_read_iov(1, 1, 0, 1, 1, &box, 0, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);
}
END_TEST

START_TEST(virgl_test_transfer_write_unbound_res)
{
  int ret;
  struct virgl_box box;

  ret = virgl_renderer_transfer_write_iov(1, 1, 0, 1, 1, &box, 0, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);
}
END_TEST

/* don't pass an IOV to read into */
START_TEST(virgl_test_transfer_read_no_iov)
{
  struct virgl_box box;
  struct virgl_renderer_resource_create_args res;
  int ret;

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  ret = virgl_renderer_transfer_read_iov(1, 1, 0, 1, 1, &box, 0, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_write_no_iov)
{
  struct virgl_box box;
  struct virgl_renderer_resource_create_args res;
  int ret;

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  ret = virgl_renderer_transfer_write_iov(1, 1, 0, 1, 1, &box, 0, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_read_no_box)
{
  struct virgl_renderer_resource_create_args res;
  struct iovec iovs[1];
  int niovs = 1;
  int ret;

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  ret = virgl_renderer_transfer_read_iov(1, 1, 0, 1, 1, NULL, 0, iovs, niovs);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_write_no_box)
{
  struct virgl_renderer_resource_create_args res;
  struct iovec iovs[1];
  int niovs = 1;
  int ret;

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  ret = virgl_renderer_transfer_write_iov(1, 1, 0, 1, 1, NULL, 0, iovs, niovs);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_read_1d_bad_box)
{
  struct virgl_renderer_resource_create_args res;
  struct iovec iovs[1];
  int niovs = 1;
  int ret;
  struct virgl_box box;

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  box.x = box.y = box.z = 0;
  box.w = 10;
  box.h = 2;
  box.d = 1;

  ret = virgl_renderer_transfer_read_iov(1, 1, 0, 1, 1, &box, 0, iovs, niovs);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_write_1d_bad_box)
{
  struct virgl_renderer_resource_create_args res;
  struct iovec iovs[1];
  int niovs = 1;
  int ret;
  struct virgl_box box;

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  box.x = box.y = box.z = 0;
  box.w = 10;
  box.h = 2;
  box.d = 1;

  ret = virgl_renderer_transfer_write_iov(1, 1, 0, 1, 1, &box, 0, iovs, niovs);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_read_1d_array_bad_box)
{
  struct virgl_renderer_resource_create_args res;
  struct iovec iovs[1];
  int niovs = 1;
  int ret;
  struct virgl_box box;

  testvirgl_init_simple_1d_resource(&res, 1);
  res.target = PIPE_TEXTURE_1D_ARRAY;
  res.array_size = 5;

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  box.x = box.y = box.z = 0;
  box.w = 10;
  box.h = 2;
  box.d = 6;

  ret = virgl_renderer_transfer_read_iov(1, 1, 0, 1, 1, &box, 0, iovs, niovs);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_read_3d_bad_box)
{
  struct virgl_renderer_resource_create_args res;
  struct iovec iovs[1];
  int niovs = 1;
  int ret;
  struct virgl_box box;

  testvirgl_init_simple_1d_resource(&res, 1);
  res.target = PIPE_TEXTURE_3D;
  res.depth = 5;

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  box.x = box.y = box.z = 0;
  box.w = 10;
  box.h = 2;
  box.d = 6;

  ret = virgl_renderer_transfer_read_iov(1, 1, 0, 1, 1, &box, 0, iovs, niovs);
  ck_assert_int_eq(ret, EINVAL);
  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
}
END_TEST

START_TEST(virgl_test_transfer_1d)
{
    struct virgl_resource res;
    unsigned char data[50*4];
    struct iovec iov = { .iov_base = data, .iov_len = sizeof(data) };
    int niovs = 1;
    int ret, i;
    struct virgl_box box;

    /* init and create simple 2D resource */
    ret = testvirgl_create_backed_simple_1d_res(&res, 1);
    ck_assert_int_eq(ret, 0);

    /* attach resource to context */
    virgl_renderer_ctx_attach_resource(1, res.handle);

    box.x = box.y = box.z = 0;
    box.w = 50;
    box.h = 1;
    box.d = 1;
    for (i = 0; i < sizeof(data); i++)
        data[i] = i;

    ret = virgl_renderer_transfer_write_iov(res.handle, 1, 0, 0, 0, &box, 0, &iov, niovs);
    ck_assert_int_eq(ret, 0);

    ret = virgl_renderer_transfer_read_iov(res.handle, 1, 0, 0, 0, &box, 0, NULL, 0);
    ck_assert_int_eq(ret, 0);

    /* check the returned values */
    unsigned char *ptr = res.iovs[0].iov_base;
    for (i = 0; i < sizeof(data); i++) {
        ck_assert_int_eq(ptr[i], i);
    }

    virgl_renderer_ctx_detach_resource(1, res.handle);
    testvirgl_destroy_backed_res(&res);
}
END_TEST

Suite *virgl_init_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("virgl_transfer");
  tc_core = tcase_create("transfer");

  tcase_add_unchecked_fixture(tc_core, testvirgl_init_single_ctx_nr, testvirgl_fini_single_ctx);
  tcase_add_test(tc_core, virgl_test_transfer_read_illegal_ctx);
  tcase_add_test(tc_core, virgl_test_transfer_write_illegal_ctx);
  tcase_add_test(tc_core, virgl_test_transfer_read_unbound_res);
  tcase_add_test(tc_core, virgl_test_transfer_write_unbound_res);
  tcase_add_test(tc_core, virgl_test_transfer_read_no_iov);
  tcase_add_test(tc_core, virgl_test_transfer_write_no_iov);
  tcase_add_test(tc_core, virgl_test_transfer_read_no_box);
  tcase_add_test(tc_core, virgl_test_transfer_write_no_box);
  tcase_add_test(tc_core, virgl_test_transfer_read_1d_bad_box);
  tcase_add_test(tc_core, virgl_test_transfer_write_1d_bad_box);
  tcase_add_test(tc_core, virgl_test_transfer_read_1d_array_bad_box);
  tcase_add_test(tc_core, virgl_test_transfer_read_3d_bad_box);
  tcase_add_test(tc_core, virgl_test_transfer_1d);

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
