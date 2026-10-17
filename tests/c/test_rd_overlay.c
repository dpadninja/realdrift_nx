/* sources: source/platform/rd_overlay.c */
/* Host unit test of rd_overlay. MIT (see LICENSE). */
#include "check.h"
#include "rd_overlay.h"

/* Each rectangle becomes two triangles covering exactly its corners, in the order the GL draw
 * expects. */
static void test_rect_to_two_triangles(void) {
  const rd_rect r[2] = { { 10, 20, 3, 4 }, { 0, 0, 1, 1 } };
  float v[2 * RD_OVERLAY_RECT_FLOATS] = {0};
  CHECK(rd_overlay_rect_tris(r, 2, v, 2) == 2);
  const float want[RD_OVERLAY_RECT_FLOATS] = { 10,20, 13,20, 10,24,  13,20, 13,24, 10,24 };
  for (int i = 0; i < RD_OVERLAY_RECT_FLOATS; i++) CHECK(v[i] == want[i]);
  CHECK(v[RD_OVERLAY_RECT_FLOATS + 8] == 1 && v[RD_OVERLAY_RECT_FLOATS + 9] == 1);  /* 2nd rect, far corner */
}

/* At most `max` rectangles are converted, nothing is written past the buffer, and zero or
 * negative counts convert nothing. */
static void test_capped_at_max(void) {
  const rd_rect r[3] = { { 0, 0, 1, 1 }, { 5, 5, 1, 1 }, { 9, 9, 1, 1 } };
  float v[3 * RD_OVERLAY_RECT_FLOATS];
  v[2 * RD_OVERLAY_RECT_FLOATS] = -7;
  CHECK(rd_overlay_rect_tris(r, 3, v, 2) == 2);
  CHECK(v[2 * RD_OVERLAY_RECT_FLOATS] == -7);                 /* nothing written past max */
  CHECK(rd_overlay_rect_tris(r, 0, v, 2) == 0);
  CHECK(rd_overlay_rect_tris(r, -1, v, 2) == 0);
}

static int g_drawn;
static void mod_draw(int w, int h) { g_drawn = w * h; }

/* The mod draw hook starts empty (no mods: the swap wrapper draws nothing extra) and returns
 * what rd_mods_boot set. */
static void test_mod_draw_hook(void) {
  CHECK(rd_overlay_mod_draw() == NULL);
  rd_overlay_set_mod_draw(mod_draw);
  rd_overlay_mod_draw()(2, 3);
  CHECK(g_drawn == 6);
}

int main(void) {
  RUN(test_rect_to_two_triangles);
  RUN(test_capped_at_max);
  RUN(test_mod_draw_hook);
  return g_failures != 0;
}
