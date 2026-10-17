/* sources: source/game/rd_race.c */
/* Host unit test of rd_race. MIT (see LICENSE). */
#include "check.h"
#include "rd_race.h"

static void test_pause_action(void) {
  CHECK(rd_pause_action(0, 0, 0, 0) == RD_PAUSE_NONE);
  CHECK(rd_pause_action(1, 0, 0, 0) == RD_PAUSE_ENABLE);
  CHECK(rd_pause_action(1, 1, 0, 0) == RD_PAUSE_DISABLE);    /* + again resumes */
  CHECK(rd_pause_action(1, 0, 1, 0) == RD_PAUSE_NONE);       /* photo mode */
  CHECK(rd_pause_action(1, 0, 0, 1) == RD_PAUSE_NONE);       /* results screen */
}


int main(void) {
  RUN(test_pause_action);
  return g_failures != 0;
}
