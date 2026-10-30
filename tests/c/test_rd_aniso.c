/* sources: source/android/rd_aniso.c */
/* Host unit test of rd_aniso. MIT (see LICENSE). */
#include "check.h"
#include "rd_aniso.h"

#define GL_LINEAR         0x2601
#define GL_TEXTURE_WRAP_S 0x2802

/* Only the config levels the extension takes are valid. */
static void test_levels(void) {
  CHECK(rd_aniso_valid(RD_ANISO_OFF) && rd_aniso_valid(2) && rd_aniso_valid(4) &&
        rd_aniso_valid(8) && rd_aniso_valid(16));
  CHECK(!rd_aniso_valid(1) && !rd_aniso_valid(3) && !rd_aniso_valid(32) && !rd_aniso_valid(-8));
}

/* Anisotropy is added after a mipmapped min filter only: a plain LINEAR texture (UI, render
 * targets) has no mips to choose from, and other parameters are left alone. */
static void test_after_mipmapped_min_filter(void) {
  CHECK(rd_aniso_after(RD_GL_TEXTURE_MIN_FILTER, RD_GL_LINEAR_MIPMAP_LINEAR));
  CHECK(rd_aniso_after(RD_GL_TEXTURE_MIN_FILTER, RD_GL_NEAREST_MIPMAP_NEAREST));
  CHECK(!rd_aniso_after(RD_GL_TEXTURE_MIN_FILTER, GL_LINEAR));
  CHECK(!rd_aniso_after(GL_TEXTURE_WRAP_S, RD_GL_LINEAR_MIPMAP_LINEAR));
}

/* The engine's own request is raised to the level, never lowered, and the GPU cap wins. */
static void test_amount(void) {
  CHECK(rd_aniso_amount(1.0f, 8, 16.0f) == 8.0f);
  CHECK(rd_aniso_amount(16.0f, 8, 16.0f) == 16.0f);
  CHECK(rd_aniso_amount(1.0f, 16, 8.0f) == 8.0f);
  CHECK(rd_aniso_amount(1.0f, 8, 0.0f) == 8.0f);   /* limit unknown: not capped */
}

int main(void) {
  RUN(test_levels);
  RUN(test_after_mipmapped_min_filter);
  RUN(test_amount);
  return g_failures != 0;
}
