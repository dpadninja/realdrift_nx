/* sources: source/platform/rd_perf.c */
/* Host unit test of rd_perf. MIT (see LICENSE). */
#include "check.h"
#include "rd_perf.h"

#define MS 1000000ull

/* auto renders 720p handheld and 1080p docked, fixed values win over the mode, and an unknown
 * value behaves like auto. */
static void test_resolution(void) {
  int w, h;
  rd_pick_resolution(RD_RES_AUTO, 0, &w, &h); CHECK(w == 1280 && h == 720);
  rd_pick_resolution(RD_RES_AUTO, 1, &w, &h); CHECK(w == 1920 && h == 1080);
  rd_pick_resolution(RD_RES_1080, 0, &w, &h); CHECK(w == 1920 && h == 1080);
  rd_pick_resolution(RD_RES_720, 1, &w, &h);  CHECK(w == 1280 && h == 720);
  rd_pick_resolution(12345, 0, &w, &h);       CHECK(w == 1280 && h == 720);   /* unknown = auto */
}

/* A boost started at boot stays on through the tail, is carried on by the first (long, loading)
 * frame, and ends a tail after the short frames begin -- the menu runs at the normal clocks. */
static void test_boost_from_boot(void) {
  const uint64_t T = RD_BOOST_THRESHOLD_NS, TAIL = RD_BOOST_TAIL_NS;
  rd_boost b = {0};
  rd_boost_start(&b, 1000 * MS, TAIL);
  CHECK(b.on);
  CHECK(rd_boost_update(&b, 1100 * MS, 1, 1050 * MS, T, TAIL));         /* frame 0, 50 ms in */
  CHECK(rd_boost_update(&b, 3000 * MS, 1, 1050 * MS, T, TAIL));         /* frame 0 is long */
  CHECK(rd_boost_update(&b, 3200 * MS, 1, 3190 * MS, T, TAIL));         /* tail */
  CHECK(!rd_boost_update(&b, 3600 * MS, 1, 3590 * MS, T, TAIL));        /* menu: off */
}

/* The CPU boost turns on only for a frame longer than the threshold (a loading frame, not a
 * 200 ms hitch) and stays on for the tail after loading ends; the boost lowers the GPU clock,
 * so it must not stick during play. */
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

/* The poller takes the time, then reads the frame's start; when the main thread starts a frame
 * in between, the start is after "now". That must not count as an endless frame: it turned the
 * boost on mid-race (GPU at 76 MHz, every few frames late; research 16). */
static void test_boost_frame_started_after_now(void) {
  rd_boost b = {0};
  CHECK(!rd_boost_update(&b, 1000 * MS, 1, 1000 * MS + 1, RD_BOOST_THRESHOLD_NS, RD_BOOST_TAIL_NS));
  CHECK(!b.on && !b.until_ns);
}

/* The boost is off before any long frame was seen. */
static void test_boost_starts_off(void) {
  rd_boost b = {0};
  CHECK(!rd_boost_update(&b, 5, 0, 0, RD_BOOST_THRESHOLD_NS, RD_BOOST_TAIL_NS));
  CHECK(!b.on);
}

int main(void) {
  RUN(test_boost_from_boot);
  RUN(test_resolution);
  RUN(test_boost_only_on_long_frames);
  RUN(test_boost_starts_off);
  RUN(test_boost_frame_started_after_now);
  return g_failures != 0;
}
