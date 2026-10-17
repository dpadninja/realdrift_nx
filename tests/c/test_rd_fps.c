/* sources: source/platform/rd_fps.c source/platform/rd_text.c */
/* Host unit test of rd_fps. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_fps.h"
#include "rd_platform.h"

#define MS RD_NS_PER_MS

/* Shows "-- FPS" until the first half-second window is full, then publishes once per window:
 * 30 frames of 16.67 ms read as 60.0 FPS with a 17 ms slowest frame. */
static void test_steady_60(void) {
  rd_fps f = RD_FPS_INIT;
  char buf[32];
  rd_fps_format(&f, buf, sizeof buf);
  CHECK(!strcmp(buf, "-- FPS"));

  uint64_t t = 1000 * MS;
  CHECK(!rd_fps_frame(&f, t, RD_FPS_WINDOW_NS));          /* first frame only starts the clock */
  int published = 0;
  for (int i = 1; i <= 30; i++) published += rd_fps_frame(&f, t + (uint64_t)i * RD_FRAME_NS, RD_FPS_WINDOW_NS);
  CHECK(published == 1);                                  /* 30 frames x 16.67 ms = one 500 ms window */
  CHECK(f.fps10 == 600 && f.worst_ms == 17);
  rd_fps_format(&f, buf, sizeof buf);
  CHECK(!strcmp(buf, "60.0 FPS 17MS"));
}

/* A single 100 ms hitch lowers the rate and shows up as the slowest frame, and the next window
 * forgets it, so the counter reflects the last half second only. */
static void test_hitch_and_reset(void) {
  rd_fps f = RD_FPS_INIT;
  uint64_t t = 5 * MS;
  rd_fps_frame(&f, t, 500 * MS);
  t += 100 * MS; CHECK(!rd_fps_frame(&f, t, 500 * MS));   /* one 100 ms hitch */
  for (int i = 0; i < 20; i++) { t += 20 * MS; rd_fps_frame(&f, t, 500 * MS); }
  CHECK(f.fps10 == 420 && f.worst_ms == 100);             /* 21 frames in 500 ms */
  for (int i = 0; i < 25; i++) { t += 20 * MS; rd_fps_frame(&f, t, 500 * MS); }
  CHECK(f.fps10 == 500 && f.worst_ms == 20);              /* the next window forgets the hitch */
}

/* The 3x5 font becomes one rectangle per horizontal run, glyphs advance by 4 pixels, unknown
 * characters draw nothing, and the output is capped at the caller's buffer size. */
static void test_layout(void) {
  rd_rect r[64];
  /* '1' = .X. / XX. / .X. / .X. / XXX -> 5 runs */
  CHECK(rd_fps_layout("1", 10, 20, 2, r, 64) == 5);
  CHECK(r[0].x == 12 && r[0].y == 20 && r[0].w == 2 && r[0].h == 2);
  CHECK(r[1].x == 10 && r[1].y == 22 && r[1].w == 4);
  CHECK(r[4].x == 10 && r[4].y == 28 && r[4].w == 6);
  /* the second glyph starts one advance later; space and unknown chars draw nothing */
  CHECK(rd_fps_layout(" ~.", 0, 0, 1, r, 64) == 1 && r[0].x == 2 * RD_FPS_ADVANCE + 1 && r[0].y == 4);
  CHECK(rd_fps_layout("8888", 0, 0, 1, r, 3) == 3);       /* capped at max */
  CHECK(rd_fps_text_width("60", 3) == 7 * 3);
  CHECK(rd_fps_text_width("", 3) == 0);
}

/* Every character rd_fps_format can produce has a glyph; a missing one would leave a silent
 * gap in the counter. */
static void test_every_char_of_the_text_has_a_glyph(void) {
  rd_rect r[256];
  const char *all = "0123456789.-FPSM";
  for (const char *c = all; *c; c++) {
    char s[2] = { *c, 0 };
    CHECK(rd_fps_layout(s, 0, 0, 1, r, 256) > 0);
  }
}

int main(void) {
  RUN(test_steady_60);
  RUN(test_hitch_and_reset);
  RUN(test_layout);
  RUN(test_every_char_of_the_text_has_a_glyph);
  return g_failures != 0;
}
