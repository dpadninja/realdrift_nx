/* sources: source/android/rd_apkpath.c */
/* Host unit test of rd_apkpath. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_apkpath.h"

static char g_last[256];
static int yes(const char *p) { strcpy(g_last, p); return 1; }
static int no(const char *p)  { strcpy(g_last, p); return 0; }

static void test_prefers_base_apk(void) {
  char buf[64];
  CHECK(strcmp(rd_package_code_path("/switch/realdrift_nx", yes, buf, sizeof buf),
               "/switch/realdrift_nx/base.apk") == 0);
  CHECK(strcmp(g_last, "/switch/realdrift_nx/base.apk") == 0);
}

static void test_falls_back_to_folder(void) {
  char buf[64];
  CHECK(strcmp(rd_package_code_path("/switch/realdrift_nx", no, buf, sizeof buf),
               "/switch/realdrift_nx") == 0);
  CHECK(strcmp(rd_package_code_path("/switch/realdrift_nx", NULL, buf, sizeof buf),
               "/switch/realdrift_nx") == 0);
}

static void test_small_buffer(void) {
  char buf[24];   /* fits the folder (20 chars) but not "/base.apk" */
  CHECK(strcmp(rd_package_code_path("/switch/realdrift_nx", yes, buf, sizeof buf),
               "/switch/realdrift_nx") == 0);
  char tiny[4];
  CHECK(rd_package_code_path("/switch/realdrift_nx", no, tiny, sizeof tiny) == NULL);
  CHECK(rd_package_code_path(NULL, no, tiny, sizeof tiny) == NULL);
}

int main(void) {
  RUN(test_prefers_base_apk);
  RUN(test_falls_back_to_folder);
  RUN(test_small_buffer);
  return g_failures != 0;
}
