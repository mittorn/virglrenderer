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
#include "virgl_hw.h"
#include "testvirgl.h"

/* pass an illegal context to transfer fn */
START_TEST(virgl_test_transfer_read_illegal_ctx)
{
  struct virgl_box box;

  virgl_renderer_transfer_read_iov(1, 2, 1, 1, 1, &box, 0, NULL, 0);
}
END_TEST

START_TEST(virgl_test_transfer_write_illegal_ctx)
{
  struct virgl_box box;

  virgl_renderer_transfer_write_iov(1, 2, 1, 1, 1, &box, 0, NULL, 0);
}
END_TEST

/* pass a resource not bound to the context to transfers */
START_TEST(virgl_test_transfer_read_unbound_res)
{
  struct virgl_box box;

  virgl_renderer_transfer_read_iov(1, 1, 1, 1, 1, &box, 0, NULL, 0);
}
END_TEST

START_TEST(virgl_test_transfer_write_unbound_res)
{
  struct virgl_box box;

  virgl_renderer_transfer_write_iov(1, 1, 1, 1, 1, &box, 0, NULL, 0);
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

  virgl_renderer_transfer_read_iov(1, 1, 1, 1, 1, &box, 0, NULL, 0);

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

  virgl_renderer_transfer_write_iov(1, 1, 1, 1, 1, &box, 0, NULL, 0);

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

  virgl_renderer_transfer_read_iov(1, 1, 1, 1, 1, NULL, 0, iovs, niovs);

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

  virgl_renderer_transfer_write_iov(1, 1, 1, 1, 1, NULL, 0, iovs, niovs);

  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
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
