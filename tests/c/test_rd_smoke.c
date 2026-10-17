/* sources: source/game/rd_smoke.c */
/* Host unit test of rd_smoke. MIT (see LICENSE). */
#include "check.h"
#include "rd_smoke.h"

/* The smoke hook does nothing at 100 % / 100 % (the default), so the original game is
 * untouched unless a knob is lowered. */
static void test_active_only_when_changed(void) {
  CHECK(!rd_smoke_active(RD_SMOKE_PERCENT_ORIG, RD_SMOKE_PERCENT_ORIG));   /* defaults: untouched */
  CHECK(rd_smoke_active(50, RD_SMOKE_PERCENT_ORIG));
  CHECK(rd_smoke_active(RD_SMOKE_PERCENT_ORIG, 50));
}

static int kept(int amount, int n) {
  int acc = 0, k = 0;
  for (int i = 0; i < n; i++) k += rd_smoke_keep(&acc, amount);
  return k;
}

/* The emit filter lets exactly `amount` of every 100 puffs through and spreads them evenly (50
 * % alternates), so the smoke thins out instead of flickering in clumps. */
static void test_keep(void) {
  CHECK(kept(100, 37) == 37);                   /* everything passes */
  CHECK(kept(50, 100) == 50);
  CHECK(kept(25, 100) == 25);
  CHECK(kept(33, 300) == 99);
  CHECK(kept(10, 9) == 0 && kept(10, 10) == 1);
  int acc = 0;                                  /* spread evenly: 50 % alternates */
  CHECK(!rd_smoke_keep(&acc, 50) && rd_smoke_keep(&acc, 50) && !rd_smoke_keep(&acc, 50));
}

/* Puff size scales linearly with the percent. */
static void test_scale_size(void) {
  CHECK(rd_smoke_scale_size(20.0f, 100) == 20.0f);
  CHECK(rd_smoke_scale_size(20.0f, 50) == 10.0f);
  CHECK(rd_smoke_scale_size(8.0f, 25) == 2.0f);
}

/* The set of smoke systems ignores duplicates and NULL and stops at its capacity instead of
 * overflowing. */
static void test_set(void) {
  static int a, b, many[RD_SMOKE_MAX_SYSTEMS + 1];
  rd_smoke_set s = {0};
  CHECK(!rd_smoke_set_has(&s, &a));
  rd_smoke_set_add(&s, &a);
  rd_smoke_set_add(&s, &a);                     /* duplicate */
  rd_smoke_set_add(&s, NULL);
  CHECK(s.n == 1 && rd_smoke_set_has(&s, &a) && !rd_smoke_set_has(&s, &b));
  for (int i = 0; i <= RD_SMOKE_MAX_SYSTEMS; i++) rd_smoke_set_add(&s, &many[i]);
  CHECK(s.n == RD_SMOKE_MAX_SYSTEMS);           /* full: the rest is ignored */
}

int main(void) {
  RUN(test_active_only_when_changed);
  RUN(test_keep);
  RUN(test_scale_size);
  RUN(test_set);
  return g_failures != 0;
}
