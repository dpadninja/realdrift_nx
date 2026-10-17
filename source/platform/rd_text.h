/* rd_text.h -- a built-in 3x5 pixel font laid out as rectangles (no font file, no texture).
 * Host-testable (tests/c/test_rd_text.c). Used by the FPS counter (rd_fps.h) and by mods'
 * on-screen text; the rectangles are drawn with rd_overlay_rects().
 *
 * Covers digits, A-Z (lower case is drawn as upper case), space and . , : ; ! ? - + = / ( ) [ ]
 * < > % # * _ ' ". Other characters are blanks.
 * MIT (see LICENSE). */
#ifndef RD_TEXT_H
#define RD_TEXT_H

#include "rd_overlay.h"   /* rd_rect */

#define RD_TEXT_GLYPH_W 3
#define RD_TEXT_GLYPH_H 5
#define RD_TEXT_ADVANCE 4   /* glyph + 1 pixel gap */

/* Lays `text` out at (x, y) (top-left, y down) with font pixels of size `px`: each lit
 * horizontal run of a glyph row becomes one rectangle. Returns the number of rectangles
 * written (at most `max`). */
int rd_text_layout(const char *text, float x, float y, float px, rd_rect *out, int max);

/* Width of `text` laid out with pixel size `px` (no trailing gap). */
float rd_text_width(const char *text, float px);

/* 1 if the font has a glyph for c (after upper-casing). */
int rd_text_has(char c);

#endif
