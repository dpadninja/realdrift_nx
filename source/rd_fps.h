/* rd_fps.h -- the debug FPS counter (config.txt debug_fps_counter).
 * Host-testable (tests/c/test_rd_fps.c): counting and text layout only; the rectangles are
 * drawn with rd_overlay_rects() from the eglSwapBuffers wrapper in imports.c.
 *
 * Every presented frame calls rd_fps_frame(); each RD_FPS_WINDOW_NS it publishes the
 * frame rate of that window and its slowest frame. rd_fps_layout() turns the text into
 * rectangles of a built-in 3x5 pixel font, so no font or texture is needed. */
#ifndef RD_FPS_H
#define RD_FPS_H

#include <stddef.h>
#include <stdint.h>
#include "rd_overlay.h"   /* rd_rect */

typedef struct {
  uint64_t window_start, last;   /* ns; last == 0: no frame seen yet */
  uint64_t worst;                /* slowest frame interval in this window, ns */
  unsigned frames;               /* frames in this window */
  int      fps10;                /* last published FPS x10, -1 before the first window */
  int      worst_ms;             /* last published slowest frame, ms */
} rd_fps;

#define RD_FPS_INIT { .fps10 = -1 }

/* Counts one presented frame at `now_ns`. Returns 1 when fps10/worst_ms were refreshed. */
int rd_fps_frame(rd_fps *f, uint64_t now_ns, uint64_t window_ns);

/* "59.9 FPS 17MS", or "-- FPS" before the first window. Returns the length. */
int rd_fps_format(const rd_fps *f, char *buf, size_t n);

#define RD_FPS_GLYPH_W 3
#define RD_FPS_GLYPH_H 5
#define RD_FPS_ADVANCE 4   /* glyph + 1 pixel gap */

/* Lays `text` out at (x, y) (top-left, y down) with font pixels of size `px`: each lit
 * horizontal run of a glyph row becomes one rectangle. Characters the font lacks are
 * blanks. Returns the number of rectangles written (at most `max`). */
int rd_fps_layout(const char *text, float x, float y, float px, rd_rect *out, int max);

/* Width of `text` laid out with pixel size `px` (no trailing gap). */
float rd_fps_text_width(const char *text, float px);

#endif
