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
 * resource tests
 * test illegal resource combinations
 - 1D resources with height or depth
 - 2D with depth
*/
#include <check.h>
#include <stdlib.h>
#include <errno.h>
#include <virglrenderer.h>
#include "testvirgl.h"

#include "pipe/p_defines.h"

/* create a buffer */
START_TEST(virgl_res_create_buffer)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_buffer(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a buffer */
START_TEST(virgl_res_create_buffer_with_height)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_buffer(&res, 1);
  res.height = 50;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST
/* create a 1D texture */
START_TEST(virgl_res_create_1d)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 1D texture with height - this should fail */
START_TEST(virgl_res_create_1d_with_height)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);

  res.height = 50;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 1D texture with depth - this should fail */
START_TEST(virgl_res_create_1d_with_depth)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);

  res.depth = 2;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 1D array texture */
START_TEST(virgl_res_create_1d_array)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);
  res.target = PIPE_TEXTURE_1D_ARRAY;
  res.array_size = 2;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 1D texture with height - this should fail */
START_TEST(virgl_res_create_1d_array_with_height)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);
  res.target = PIPE_TEXTURE_1D_ARRAY;
  res.array_size = 2;
  res.height = 50;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 1D texture with depth - this should fail */
START_TEST(virgl_res_create_1d_array_with_depth)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);
  res.target = PIPE_TEXTURE_1D_ARRAY;
  res.array_size = 2;
  res.depth = 2;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 2D texture */
START_TEST(virgl_res_create_2d)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_2d_resource(&res, 1);
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 2D texture with depth - this should fail */
START_TEST(virgl_res_create_2d_with_depth)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);

  res.depth = 2;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 2D Array texture */
START_TEST(virgl_res_create_2d_array)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_2d_resource(&res, 1);
  res.target = PIPE_TEXTURE_2D_ARRAY;
  res.array_size = 2;

  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, 0);

  testvirgl_fini_single_ctx();
}
END_TEST

/* create a 2D array texture with depth - this should fail */
START_TEST(virgl_res_create_2d_array_with_depth)
{
  int ret;
  struct virgl_renderer_resource_create_args res;

  ret = testvirgl_init_single_ctx();
  ck_assert_int_eq(ret, 0);

  testvirgl_init_simple_1d_resource(&res, 1);
  res.target = PIPE_TEXTURE_2D_ARRAY;
  res.array_size = 2;

  res.depth = 2;
  ret = virgl_renderer_resource_create(&res, NULL, 0);
  ck_assert_int_eq(ret, EINVAL);

  testvirgl_fini_single_ctx();
}
END_TEST

Suite *virgl_init_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("virgl_resource");
  tc_core = tcase_create("resource");

  tcase_add_test(tc_core, virgl_res_create_buffer);
  tcase_add_test(tc_core, virgl_res_create_buffer_with_height);
  tcase_add_test(tc_core, virgl_res_create_1d);
  tcase_add_test(tc_core, virgl_res_create_1d_with_height);
  tcase_add_test(tc_core, virgl_res_create_1d_with_depth);
  tcase_add_test(tc_core, virgl_res_create_1d_array);
  tcase_add_test(tc_core, virgl_res_create_1d_array_with_height);
  tcase_add_test(tc_core, virgl_res_create_1d_array_with_depth);
  tcase_add_test(tc_core, virgl_res_create_2d);
  tcase_add_test(tc_core, virgl_res_create_2d_with_depth);
  tcase_add_test(tc_core, virgl_res_create_2d_array);
  tcase_add_test(tc_core, virgl_res_create_2d_array_with_depth);
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
