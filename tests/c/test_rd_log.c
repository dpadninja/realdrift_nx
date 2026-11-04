/* sources: source/loader/rd_log.c */
/* Host unit test of rd_log. MIT (see LICENSE). */
#include "check.h"
#include "rd_log.h"

/* debug_log=1 keeps every line (NULL format excepted), so a bug report has the whole story. */
static void test_full_keeps_everything(void) {
  CHECK(rd_log_wanted(RD_LOG_FULL, "[io] open(%s)\n"));
  CHECK(rd_log_wanted(RD_LOG_FULL, "[perf] frame %d\n"));
  CHECK(!rd_log_wanted(RD_LOG_FULL, NULL));
}

/* debug_log=0 keeps crashes, stalls, the engine's stdio, the build banner, GC stops and Unity
 * exceptions, and drops the high-volume tags that cost SD writes. */
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

/* Only crash and watchdog lines are flushed at once: they may be the last thing written before
 * the process dies, while the rest stays buffered. */
static void test_urgent(void) {
  CHECK(rd_log_urgent("[crash] ===="));
  CHECK(rd_log_urgent("[wd]   UnityMain"));
  CHECK(!rd_log_urgent("[gc] stop-the-world"));
  CHECK(!rd_log_urgent("[perf] frame"));
  CHECK(!rd_log_urgent(NULL));
}

/* dlsym/dlopen/JNI chatter is always dropped, and per-method [jni] lines only when RD_LOG_JNI
 * is off; other tags are never treated as noise. */
static void test_noisy(void) {
  CHECK(rd_log_noisy("dlsym(%s)\n", 1));
  CHECK(rd_log_noisy("dlopen(%s)\n", 1));
  CHECK(rd_log_noisy("JNI: %s\n", 1));
  CHECK(rd_log_noisy(RD_LOG_TAG_JNI " call: %s\n", 0));
  CHECK(!rd_log_noisy(RD_LOG_TAG_JNI " call: %s\n", 1));
  CHECK(!rd_log_noisy(RD_LOG_TAG_GC " stop\n", 0));
  CHECK(!rd_log_noisy(NULL, 0));
}

/* The lines diag.c renders from "%s ===== %s on thread %s" (abort, stall) are urgent and kept
 * at debug_log=0 once rendered: debugPrintf filters the rendered line, because the format's
 * leading "%s" hid the tag and an abort() lost the report in the buffer (2026-11-03). */
static void test_rendered_diag_lines(void) {
  CHECK(!rd_log_urgent("%s ===== %s on thread %s, frame %d =====\n"));
  CHECK(rd_log_urgent(RD_LOG_TAG_CRASH " ===== abort() on thread UnityMain, frame 94770 =====\n"));
  CHECK(rd_log_wanted(RD_LOG_MINIMAL, RD_LOG_TAG_CRASH "     bt[0] libil2cpp.so+0x754bc4\n"));
  CHECK(rd_log_urgent(RD_LOG_TAG_WD "   UnityMain  PC=libunity.so+0x1234\n"));
}

int main(void) {
  RUN(test_noisy);
  RUN(test_full_keeps_everything);
  RUN(test_minimal_keeps_only_important);
  RUN(test_urgent);
  RUN(test_rendered_diag_lines);
  return g_failures != 0;
}
