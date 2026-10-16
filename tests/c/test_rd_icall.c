/* sources: source/rd_icall.c */
#include <string.h>
#include "check.h"
#include "rd_icall.h"

static int ours_touch(void) { return 1; }
static int ours_mouse(void) { return 2; }
static int engine_fn(void) { return 3; }

static const rd_icall_override TABLE[] = {
  { "UnityEngine.Input::get_touchCount", (void *)ours_touch },
  { "UnityEngine.Input::GetMouseButton", (void *)ours_mouse },
  { NULL, NULL },
};

static const char *g_name[8];
static const void *g_fn[8];
static int g_n;
static void fake_real(const char *name, const void *fn) { g_name[g_n] = name; g_fn[g_n] = fn; g_n++; }

static void test_lookup(void) {
  CHECK(rd_icall_lookup(TABLE, "UnityEngine.Input::get_touchCount") == (void *)ours_touch);
  CHECK(rd_icall_lookup(TABLE, "UnityEngine.Input::GetMouseButtonDown") == NULL);  /* exact match */
  CHECK(rd_icall_lookup(TABLE, NULL) == NULL);
  CHECK(rd_icall_lookup(NULL, "x") == NULL);
}

static void test_wrapper_substitutes_known_and_forwards_others(void) {
  g_n = 0;
  rd_icall_setup(TABLE, fake_real);
  rd_add_internal_call("UnityEngine.Input::get_touchCount", (void *)engine_fn);
  rd_add_internal_call("UnityEngine.Time::get_deltaTime", (void *)engine_fn);
  CHECK(g_n == 2);
  CHECK(g_fn[0] == (void *)ours_touch);
  CHECK(strcmp(g_name[0], "UnityEngine.Input::get_touchCount") == 0);
  CHECK(g_fn[1] == (void *)engine_fn);
  CHECK(rd_icall_applied() == 1);
}

static void test_without_real_does_not_crash(void) {
  rd_icall_setup(TABLE, NULL);
  rd_add_internal_call("UnityEngine.Input::get_touchCount", (void *)engine_fn);
  CHECK(rd_icall_applied() == 1);
}

int main(void) {
  RUN(test_lookup);
  RUN(test_wrapper_substitutes_known_and_forwards_others);
  RUN(test_without_real_does_not_crash);
  return g_failures != 0;
}
