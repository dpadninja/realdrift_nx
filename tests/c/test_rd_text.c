/* sources: source/platform/rd_text.c */
/* Host unit test of rd_text, the 3x5 font mods draw text with. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_text.h"

/* Every character the header promises has a glyph and draws something; a missing one would be
 * a silent gap in a mod's text. Lower case maps to upper case. */
static void test_documented_characters_have_glyphs(void) {
  const char *all = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.,:;!?-+=/()[]<>%#*_'\"";
  rd_rect r[32];
  for (const char *c = all; *c; c++) {
    char s[2] = { *c, 0 };
    CHECK(rd_text_has(*c) && rd_text_layout(s, 0, 0, 1, r, 32) > 0);
  }
  CHECK(rd_text_has('a') && rd_text_has('z'));
  CHECK(!rd_text_has('~') && !rd_text_has(' '));
}

/* Lower and upper case lay out identically, glyphs advance by RD_TEXT_ADVANCE, unknown
 * characters keep their slot, and the output is capped at max. */
static void test_layout(void) {
  rd_rect a[64], b[64];
  const int n = rd_text_layout("Hi", 0, 0, 2, a, 64);
  CHECK(n > 0 && n == rd_text_layout("HI", 0, 0, 2, b, 64) && !memcmp(a, b, sizeof(rd_rect) * (size_t)n));
  CHECK(rd_text_layout("~I", 0, 0, 1, a, 64) > 0 && a[0].x == RD_TEXT_ADVANCE);   /* I's top bar */
  CHECK(rd_text_layout("WWWW", 0, 0, 1, a, 3) == 3);
  CHECK(rd_text_width("ABC", 2) == (3 * RD_TEXT_ADVANCE - 1) * 2);
  CHECK(rd_text_width("", 2) == 0);
}

int main(void) {
  RUN(test_documented_characters_have_glyphs);
  RUN(test_layout);
  return g_failures != 0;
}
