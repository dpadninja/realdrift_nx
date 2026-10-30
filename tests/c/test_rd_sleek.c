/* sources: source/game/rd_sleek.c */
/* Host unit test of rd_sleek. MIT (see LICENSE). */
#include "check.h"
#include "rd_sleek.h"

/* A flag is written only when it differs (so the change is logged once); any non-zero `want`
 * means true. */
static void test_set_flag(void) {
  uint8_t f = 1;
  CHECK(rd_sleek_set_flag(&f, 0) == 1 && f == 0);
  CHECK(rd_sleek_set_flag(&f, 0) == 0 && f == 0);
  CHECK(rd_sleek_set_flag(&f, 1) == 1 && f == 1);
  CHECK(rd_sleek_set_flag(&f, 5) == 0 && f == 1);
}

int main(void) {
  RUN(test_set_flag);
  return g_failures != 0;
}
