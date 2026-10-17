/* sources: source/platform/rd_perf.c */
#include "check.h"
#include "rd_perf.h"

#define MS 1000000ull

static void test_resolution(void) {
  int w, h;
  rd_pick_resolution(RD_RES_AUTO, 0, &w, &h); CHECK(w == 1280 && h == 720);
  rd_pick_resolution(RD_RES_AUTO, 1, &w, &h); CHECK(w == 1920 && h == 1080);
  rd_pick_resolution(RD_RES_1080, 0, &w, &h); CHECK(w == 1920 && h == 1080);
  rd_pick_resolution(RD_RES_720, 1, &w, &h);  CHECK(w == 1280 && h == 720);
  rd_pick_resolution(12345, 0, &w, &h);       CHECK(w == 1280 && h == 720);   /* unknown = auto */
}

static void test_boost_only_on_long_frames(void) {
  rd_boost b = {0};
  const uint64_t T = RD_BOOST_THRESHOLD_NS, TAIL = RD_BOOST_TAIL_NS;
  CHECK(!rd_boost_update(&b, 1000 * MS, 1, 990 * MS, T, TAIL));        /* normal 10ms frame */
  CHECK(!rd_boost_update(&b, 1100 * MS, 1, 900 * MS, T, TAIL));        /* 200ms hitch: no */
  CHECK(rd_boost_update(&b, 2300 * MS, 1, 2000 * MS, T, TAIL));        /* 300ms: loading */
  CHECK(rd_boost_update(&b, 7000 * MS, 1, 2000 * MS, T, TAIL));        /* still loading */
  CHECK(rd_boost_update(&b, 7100 * MS, 0, 0, T, TAIL));                /* tail keeps it on */
  CHECK(rd_boost_update(&b, 7499 * MS, 1, 7490 * MS, T, TAIL));
  CHECK(!rd_boost_update(&b, 7501 * MS, 1, 7495 * MS, T, TAIL));       /* tail expired */
}

static void test_boost_starts_off(void) {
  rd_boost b = {0};
  CHECK(!rd_boost_update(&b, 5, 0, 0, RD_BOOST_THRESHOLD_NS, RD_BOOST_TAIL_NS));
  CHECK(!b.on);
}

int main(void) {
  RUN(test_resolution);
  RUN(test_boost_only_on_long_frames);
  RUN(test_boost_starts_off);
  return g_failures != 0;
}
