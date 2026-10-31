/* sources: source/loader/rd_wdclock.c */
/* Host unit test of rd_wdclock. MIT (see LICENSE). */
#include "check.h"
#include "rd_wdclock.h"

/* A normal wait is now - since. */
static void test_elapsed_normal(void) {
  CHECK(rd_wd_elapsed(1000, 400) == 600);
}

/* A wait stamped after the watchdog read the clock is 0, not a wrapped huge value (the
 * dump printed 1537228672.8 s for such threads). */
static void test_elapsed_since_after_now_is_zero(void) {
  CHECK(rd_wd_elapsed(1000, 1001) == 0);
  CHECK(rd_wd_elapsed(1000, 1000) == 0);
}

/* An unset start (0) means no wait in progress. */
static void test_elapsed_unset_is_zero(void) {
  CHECK(rd_wd_elapsed(1000, 0) == 0);
}

/* A poll that overslept its limit means the process was suspended (HOME, sleep); a normal
 * poll, a clock read out of order or no previous poll does not. */
static void test_suspended(void) {
  CHECK(rd_wd_was_suspended(100, 5000, 3000));
  CHECK(!rd_wd_was_suspended(100, 1100, 3000));
  CHECK(!rd_wd_was_suspended(100, 3100, 3000));
  CHECK(!rd_wd_was_suspended(5000, 100, 3000));
  CHECK(!rd_wd_was_suspended(0, 5000, 3000));
}

int main(void) {
  RUN(test_elapsed_normal);
  RUN(test_elapsed_since_after_now_is_zero);
  RUN(test_elapsed_unset_is_zero);
  RUN(test_suspended);
  return g_failures != 0;
}
