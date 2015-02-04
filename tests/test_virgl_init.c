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

/*
 * basic library initialisation, teardown, reset
 * and context creation tests.
 */

#include <check.h>
#include <stdlib.h>
#include <virglrenderer.h>

#include "testvirgl.h"
#include "virgl_hw.h"
struct myinfo_struct {
  uint32_t test;
};

struct myinfo_struct mystruct;

static struct virgl_renderer_callbacks test_cbs;

START_TEST(virgl_init_no_cbs)
{
  int ret;
  ret = virgl_renderer_init(&mystruct, 0, NULL);
  ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(virgl_init_no_cookie)
{
  int ret;
  ret = virgl_renderer_init(NULL, 0, &test_cbs);
  ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(virgl_init_cbs_wrong_ver)
{
  int ret;
  struct virgl_renderer_callbacks testcbs;
  memset(&testcbs, 0, sizeof(testcbs));
  testcbs.version = 2;
  ret = virgl_renderer_init(&mystruct, 0, &testcbs);
  ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(virgl_init_egl)
{
  int ret;
  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);
  virgl_renderer_cleanup(&mystruct);
}

END_TEST

START_TEST(virgl_init_egl_create_ctx)
{
  int ret;
  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);
  ret = virgl_renderer_context_create(1, strlen("test1"), "test1");
  ck_assert_int_eq(ret, 0);

  virgl_renderer_context_destroy(1);
  virgl_renderer_cleanup(&mystruct);
}
END_TEST

START_TEST(virgl_init_egl_create_ctx_leak)
{
  int ret;
  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);
  ret = virgl_renderer_context_create(1, strlen("test1"), "test1");
  ck_assert_int_eq(ret, 0);

  /* don't destroy the context - leak it make sure cleanup catches it */
  /*virgl_renderer_context_destroy(1);*/
  virgl_renderer_cleanup(&mystruct);
}
END_TEST

START_TEST(virgl_init_egl_create_ctx_create_bind_res)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);
  ret = virgl_renderer_context_create(1, strlen("test1"), "test1");
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  virgl_renderer_ctx_detach_resource(1, res.handle);

  virgl_renderer_resource_unref(1);
  virgl_renderer_context_destroy(1);
  virgl_renderer_cleanup(&mystruct);
}
END_TEST

START_TEST(virgl_init_egl_create_ctx_create_bind_res_leak)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);
  ret = virgl_renderer_context_create(1, strlen("test1"), "test1");
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_ctx_attach_resource(1, res.handle);

  /*virgl_renderer_ctx_detach_resource(1, res.handle);*/

  /*virgl_renderer_resource_unref(1);*/
  /* don't detach or destroy resource - it should still get cleanedup */
  virgl_renderer_context_destroy(1);
  virgl_renderer_cleanup(&mystruct);
}
END_TEST

START_TEST(virgl_init_egl_create_ctx_reset)
{
  int ret;
  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);
  ret = virgl_renderer_context_create(1, strlen("test1"), "test1");
  ck_assert_int_eq(ret, 0);

  virgl_renderer_reset();

  /* reset should have destroyed the context */
  ret = virgl_renderer_context_create(1, strlen("test1"), "test1");
  ck_assert_int_eq(ret, 0);
  virgl_renderer_cleanup(&mystruct);
}
END_TEST

START_TEST(virgl_init_get_caps_set0)
{
  int ret;
  uint32_t max_ver, max_size;

  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_get_cap_set(0, &max_ver, &max_size);
  ck_assert_int_eq(max_ver, 0);
  ck_assert_int_eq(max_size, 0);

  virgl_renderer_cleanup(&mystruct);
}
END_TEST

START_TEST(virgl_init_get_caps_set1)
{
  int ret;
  uint32_t max_ver, max_size;
  void *caps;
  test_cbs.version = 1;
  ret = virgl_renderer_init(&mystruct, VIRGL_RENDERER_USE_EGL, &test_cbs);
  ck_assert_int_eq(ret, 0);

  virgl_renderer_get_cap_set(1, &max_ver, &max_size);
  ck_assert_int_eq(max_ver, 1);
  ck_assert_int_ne(max_size, 0);
  ck_assert_int_eq(max_size, sizeof(struct virgl_caps_v1));

  caps = malloc(max_size);

  virgl_renderer_fill_caps(0, 0, caps);

  free(caps);
  virgl_renderer_cleanup(&mystruct);
}
END_TEST

Suite *virgl_init_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("virgl_init");
  tc_core = tcase_create("init");

  tcase_add_test(tc_core, virgl_init_no_cbs);
  tcase_add_test(tc_core, virgl_init_no_cookie);
  tcase_add_test(tc_core, virgl_init_cbs_wrong_ver);
  tcase_add_test(tc_core, virgl_init_egl);
  tcase_add_test(tc_core, virgl_init_egl_create_ctx);
  tcase_add_test(tc_core, virgl_init_egl_create_ctx_leak);
  tcase_add_test(tc_core, virgl_init_egl_create_ctx_create_bind_res);
  tcase_add_test(tc_core, virgl_init_egl_create_ctx_create_bind_res_leak);
  tcase_add_test(tc_core, virgl_init_egl_create_ctx_reset);
  tcase_add_test(tc_core, virgl_init_get_caps_set0);
  tcase_add_test(tc_core, virgl_init_get_caps_set1);
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
