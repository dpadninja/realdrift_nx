/* sources: source/rd_log.c */
#include "check.h"
#include "rd_log.h"

static void test_full_keeps_everything(void) {
  CHECK(rd_log_wanted(RD_LOG_FULL, "[io] open(%s)\n"));
  CHECK(rd_log_wanted(RD_LOG_FULL, "[perf] frame %d\n"));
  CHECK(!rd_log_wanted(RD_LOG_FULL, NULL));
}

static void test_minimal_keeps_only_important(void) {
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, "[crash] pc=%s\n"));
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, "[wd] ===== STALL #%d\n"));
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, "stdio: %s"));
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, "[boot] ===== realdrift_nx build %s %s =====\n"));
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, "[gc] stop-the-world #%llu\n"));
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, "Unity: NullReferenceException: %s"));
  CHECK(!rd_log_wanted(RD_LOG_MINIMAL, "Unity: LoadData\n"));
  CHECK(!rd_log_wanted(RD_LOG_MINIMAL, "[boot] frame %d rendered\n"));
  CHECK(!rd_log_wanted(RD_LOG_MINIMAL, "[io] fopen(%s,%s) -> %p\n"));
  CHECK(!rd_log_wanted(RD_LOG_MINIMAL, "[prefs] apply\n"));
  CHECK(!rd_log_wanted(RD_LOG_MINIMAL, "[perf] frame %d took %ldms\n"));
}

static void test_urgent(void) {
  CHECK(rd_log_urgent("[crash] ===="));
  CHECK(rd_log_urgent("[wd]   UnityMain"));
  CHECK(!rd_log_urgent("[gc] stop-the-world"));
  CHECK(!rd_log_urgent("[perf] frame"));
  CHECK(!rd_log_urgent(NULL));
}

static void test_noisy(void) {
  CHECK(rd_log_noisy("dlsym(%s)\n", 1));
  CHECK(rd_log_noisy("dlopen(%s)\n", 1));
  CHECK(rd_log_noisy("JNI: %s\n", 1));
  CHECK(rd_log_noisy(RD_LOG_TAG_JNI " call: %s\n", 0));
  CHECK(!rd_log_noisy(RD_LOG_TAG_JNI " call: %s\n", 1));
  CHECK(!rd_log_noisy(RD_LOG_TAG_GC " stop\n", 0));
  CHECK(!rd_log_noisy(NULL, 0));
}

int main(void) {
  RUN(test_noisy);
  RUN(test_full_keeps_everything);
  RUN(test_minimal_keeps_only_important);
  RUN(test_urgent);
  return g_failures != 0;
}
