/* sources: source/game/rd_render.c */
/* Host unit test of rd_render. MIT (see LICENSE). */
#include "check.h"
#include "rd_render.h"

/* The far plane only grows: a longer scene plane is kept, 0 keeps the scene's own. */
static void test_far_only_grows(void) {
  CHECK(rd_render_far(360.0f, 1500) == 1500.0f);    /* the mountain tracks */
  CHECK(rd_render_far(4000.0f, 1500) == 4000.0f);   /* Desert: already farther */
  CHECK(rd_render_far(1500.0f, 1500) == 1500.0f);
  CHECK(rd_render_far(360.0f, RD_DRAW_DISTANCE_GAME) == 360.0f);
}

/* Only the sample counts Unity's QualitySettings accepts are valid. */
static void test_aa_values(void) {
  CHECK(rd_render_aa_valid(0) && rd_render_aa_valid(2) && rd_render_aa_valid(4) &&
        rd_render_aa_valid(8));
  CHECK(!rd_render_aa_valid(1) && !rd_render_aa_valid(3) && !rd_render_aa_valid(16) &&
        !rd_render_aa_valid(-2));
}

int main(void) {
  RUN(test_far_only_grows);
  RUN(test_aa_values);
  return g_failures != 0;
}
