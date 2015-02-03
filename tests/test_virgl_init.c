#include <check.h>
#include <stdlib.h>
#include <virglrenderer.h>

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

  res.handle = 1;
  res.target = 1;
  res.format = 2;
  res.width = 50;
  res.height = 50;
  res.depth = 1;
  res.array_size = 1;
  res.last_level = 0;
  res.nr_samples = 0;
  res.bind = 0;
  res.flags = 0;

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

  res.handle = 1;
  res.target = 1;
  res.format = 2;
  res.width = 50;
  res.height = 50;
  res.depth = 1;
  res.array_size = 1;
  res.last_level = 0;
  res.nr_samples = 0;
  res.bind = 0;
  res.flags = 0;

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
