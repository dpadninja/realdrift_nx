/* rd_fps.c -- see rd_fps.h. MIT (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include "rd_fps.h"
#include "rd_platform.h"

int rd_fps_frame(rd_fps *f, uint64_t now_ns, uint64_t window_ns) {
  if (!f->last) { f->window_start = f->last = now_ns; return 0; }
  const uint64_t dt = now_ns - f->last;
  if (dt > f->worst) f->worst = dt;
  f->frames++;
  f->last = now_ns;

  const uint64_t elapsed = now_ns - f->window_start;
  if (elapsed < window_ns) return 0;
  f->fps10    = (int)(((uint64_t)f->frames * 10 * RD_NS_PER_S + elapsed / 2) / elapsed);
  f->worst_ms = (int)((f->worst + RD_NS_PER_MS / 2) / RD_NS_PER_MS);
  f->window_start = now_ns;
  f->frames = 0;
  f->worst  = 0;
  return 1;
}

int rd_fps_format(const rd_fps *f, char *buf, size_t n) {
  int w = f->fps10 < 0
        ? snprintf(buf, n, "-- FPS")
        : snprintf(buf, n, "%d.%d FPS %dMS", f->fps10 / 10, f->fps10 % 10, f->worst_ms);
  return w < 0 ? 0 : w;
}

/* 3x5 glyphs, one row per entry, bit 2 = leftmost pixel. */
static const struct { char c; unsigned char row[RD_FPS_GLYPH_H]; } FONT[] = {
  { '0', { 7, 5, 5, 5, 7 } }, { '1', { 2, 6, 2, 2, 7 } }, { '2', { 7, 1, 7, 4, 7 } },
  { '3', { 7, 1, 7, 1, 7 } }, { '4', { 5, 5, 7, 1, 1 } }, { '5', { 7, 4, 7, 1, 7 } },
  { '6', { 7, 4, 7, 5, 7 } }, { '7', { 7, 1, 1, 1, 1 } }, { '8', { 7, 5, 7, 5, 7 } },
  { '9', { 7, 5, 7, 1, 7 } }, { '.', { 0, 0, 0, 0, 2 } }, { '-', { 0, 0, 7, 0, 0 } },
  { 'F', { 7, 4, 6, 4, 4 } }, { 'P', { 7, 5, 7, 4, 4 } }, { 'S', { 7, 4, 7, 1, 7 } },
  { 'M', { 5, 7, 7, 5, 5 } },
};

/* The five rows of c in the 3x5 font, or NULL if the font has no such character. */
static const unsigned char *glyph(char c) {
  for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
    if (FONT[i].c == c) return FONT[i].row;
  return NULL;
}

int rd_fps_layout(const char *text, float x, float y, float px, rd_rect *out, int max) {
  int n = 0;
  for (int ci = 0; text[ci]; ci++) {
    const unsigned char *g = glyph(text[ci]);
    if (!g) continue;
    const float gx = x + (float)(ci * RD_FPS_ADVANCE) * px;
    for (int r = 0; r < RD_FPS_GLYPH_H; r++) {
      for (int c = 0; c < RD_FPS_GLYPH_W; ) {
        if (!(g[r] & (4 >> c))) { c++; continue; }
        int start = c;
        while (c < RD_FPS_GLYPH_W && (g[r] & (4 >> c))) c++;
        if (n == max) return n;
        out[n++] = (rd_rect){ gx + (float)start * px, y + (float)r * px,
                              (float)(c - start) * px, px };
      }
    }
  }
  return n;
}

float rd_fps_text_width(const char *text, float px) {
  size_t len = strlen(text);
  return len ? (float)(len * RD_FPS_ADVANCE - 1) * px : 0.0f;
}
