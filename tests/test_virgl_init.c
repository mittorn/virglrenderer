#include <check.h>
#include <stdlib.h>
#include <virglrenderer.h>

static void *mycookie;

static struct virgl_renderer_callbacks rcbs;

START_TEST(virgl_init_no_cbs)
{
  int ret;
  ret = virgl_renderer_init(mycookie, 0, NULL);
  ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(virgl_init_no_cookie)
{
  int ret;
  ret = virgl_renderer_init(NULL, 0, &rcbs);
  ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(virgl_init_cbs_wrong_ver)
{
  int ret;
  struct virgl_renderer_callbacks testcbs;
  memset(&testcbs, 0, sizeof(testcbs));
  testcbs.version = 2;
  ret = virgl_renderer_init(mycookie, 0, &testcbs);
  ck_assert_int_eq(ret, -1);
}
END_TEST

START_TEST(virgl_init_egl)
{
  int ret;
  rcbs.version = 1;
  ret = virgl_renderer_init(mycookie, VIRGL_RENDERER_USE_EGL, &rcbs);
  ck_assert_int_eq(ret, 0);
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
