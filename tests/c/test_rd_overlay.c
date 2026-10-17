/* sources: source/platform/rd_overlay.c */
/* Host unit test of rd_overlay. MIT (see LICENSE). */
#include "check.h"
#include "rd_overlay.h"

static void test_rect_to_two_triangles(void) {
  const rd_rect r[2] = { { 10, 20, 3, 4 }, { 0, 0, 1, 1 } };
  float v[2 * RD_OVERLAY_RECT_FLOATS] = {0};
  CHECK(rd_overlay_rect_tris(r, 2, v, 2) == 2);
  const float want[RD_OVERLAY_RECT_FLOATS] = { 10,20, 13,20, 10,24,  13,20, 13,24, 10,24 };
  for (int i = 0; i < RD_OVERLAY_RECT_FLOATS; i++) CHECK(v[i] == want[i]);
  CHECK(v[RD_OVERLAY_RECT_FLOATS + 8] == 1 && v[RD_OVERLAY_RECT_FLOATS + 9] == 1);  /* 2nd rect, far corner */
}

static void test_capped_at_max(void) {
  const rd_rect r[3] = { { 0, 0, 1, 1 }, { 5, 5, 1, 1 }, { 9, 9, 1, 1 } };
  float v[3 * RD_OVERLAY_RECT_FLOATS];
  v[2 * RD_OVERLAY_RECT_FLOATS] = -7;
  CHECK(rd_overlay_rect_tris(r, 3, v, 2) == 2);
  CHECK(v[2 * RD_OVERLAY_RECT_FLOATS] == -7);                 /* nothing written past max */
  CHECK(rd_overlay_rect_tris(r, 0, v, 2) == 0);
  CHECK(rd_overlay_rect_tris(r, -1, v, 2) == 0);
}

int main(void) {
  RUN(test_rect_to_two_triangles);
  RUN(test_capped_at_max);
  return g_failures != 0;
}
