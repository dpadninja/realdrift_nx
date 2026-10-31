/* sources: source/android/rd_gldebug.c */
/* Host unit test of rd_gldebug. MIT (see LICENSE). */
#include "check.h"
#include "rd_gldebug.h"

/* Only errors and undefined behaviour are logged; performance and other notes are not. */
static void test_wanted(void) {
  CHECK(rd_gldebug_wanted(RD_GL_DEBUG_TYPE_ERROR));
  CHECK(rd_gldebug_wanted(RD_GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR));
  CHECK(!rd_gldebug_wanted(0x8250));   /* GL_DEBUG_TYPE_PERFORMANCE */
  CHECK(!rd_gldebug_wanted(0x8251));   /* GL_DEBUG_TYPE_OTHER */
}

/* The same message from the same caller counts up; another caller or another message is a new
 * pair, so each source of an error shows up in the log at least once. */
static void test_count_pairs(void) {
  static rd_gldebug_seen s;
  CHECK(rd_gldebug_count(&s, 7, 0x1000) == 1);
  CHECK(rd_gldebug_count(&s, 7, 0x1000) == 2);
  CHECK(rd_gldebug_count(&s, 7, 0x2000) == 1);
  CHECK(rd_gldebug_count(&s, 8, 0x1000) == 1);
  CHECK(rd_gldebug_count(&s, 0, 0) == 1);       /* no caller found is still a pair */
  CHECK(rd_gldebug_count(&s, 7, 0x1000) == 3);
}

/* When every slot is taken, new pairs count together in overflow instead of evicting. */
static void test_count_overflow(void) {
  static rd_gldebug_seen s;
  for (uint32_t i = 0; i < RD_GLDEBUG_SLOTS; i++) CHECK(rd_gldebug_count(&s, i, 0x40) == 1);
  CHECK(rd_gldebug_count(&s, 9999, 0x40) == 1);
  CHECK(rd_gldebug_count(&s, 9998, 0x40) == 2);
  CHECK(s.overflow == 2);
  CHECK(rd_gldebug_count(&s, 3, 0x40) == 2);    /* known pairs still count */
}

/* The first occurrence and every RD_GLDEBUG_EVERY-th are logged, so 1700 repeats are a few
 * lines that still show the rate. */
static void test_logged(void) {
  CHECK(rd_gldebug_logged(1));
  CHECK(!rd_gldebug_logged(2));
  CHECK(!rd_gldebug_logged(RD_GLDEBUG_EVERY - 1));
  CHECK(rd_gldebug_logged(RD_GLDEBUG_EVERY));
  CHECK(rd_gldebug_logged(2 * RD_GLDEBUG_EVERY));
  CHECK(!rd_gldebug_logged(0));
}

int main(void) {
  RUN(test_wanted);
  RUN(test_count_pairs);
  RUN(test_count_overflow);
  RUN(test_logged);
  return g_failures != 0;
}
