/* rd_text.c -- see rd_text.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_text.h"

/* 3x5 glyphs, one row per entry, bit 2 = leftmost pixel. */
static const struct { char c; unsigned char row[RD_TEXT_GLYPH_H]; } FONT[] = {
  { '0', { 7, 5, 5, 5, 7 } }, { '1', { 2, 6, 2, 2, 7 } }, { '2', { 7, 1, 7, 4, 7 } },
  { '3', { 7, 1, 7, 1, 7 } }, { '4', { 5, 5, 7, 1, 1 } }, { '5', { 7, 4, 7, 1, 7 } },
  { '6', { 7, 4, 7, 5, 7 } }, { '7', { 7, 1, 1, 1, 1 } }, { '8', { 7, 5, 7, 5, 7 } },
  { '9', { 7, 5, 7, 1, 7 } },
  { 'A', { 2, 5, 7, 5, 5 } }, { 'B', { 6, 5, 6, 5, 6 } }, { 'C', { 3, 4, 4, 4, 3 } },
  { 'D', { 6, 5, 5, 5, 6 } }, { 'E', { 7, 4, 6, 4, 7 } }, { 'F', { 7, 4, 6, 4, 4 } },
  { 'G', { 3, 4, 5, 5, 3 } }, { 'H', { 5, 5, 7, 5, 5 } }, { 'I', { 7, 2, 2, 2, 7 } },
  { 'J', { 1, 1, 1, 5, 2 } }, { 'K', { 5, 5, 6, 5, 5 } }, { 'L', { 4, 4, 4, 4, 7 } },
  { 'M', { 5, 7, 7, 5, 5 } }, { 'N', { 6, 5, 5, 5, 5 } }, { 'O', { 2, 5, 5, 5, 2 } },
  { 'P', { 7, 5, 7, 4, 4 } }, { 'Q', { 2, 5, 5, 6, 3 } }, { 'R', { 6, 5, 6, 5, 5 } },
  { 'S', { 7, 4, 7, 1, 7 } }, { 'T', { 7, 2, 2, 2, 2 } }, { 'U', { 5, 5, 5, 5, 7 } },
  { 'V', { 5, 5, 5, 5, 2 } }, { 'W', { 5, 5, 7, 7, 5 } }, { 'X', { 5, 5, 2, 5, 5 } },
  { 'Y', { 5, 5, 2, 2, 2 } }, { 'Z', { 7, 1, 2, 4, 7 } },
  { '.', { 0, 0, 0, 0, 2 } }, { ',', { 0, 0, 0, 2, 4 } }, { ':', { 0, 2, 0, 2, 0 } },
  { ';', { 0, 2, 0, 2, 4 } }, { '!', { 2, 2, 2, 0, 2 } }, { '?', { 6, 1, 2, 0, 2 } },
  { '-', { 0, 0, 7, 0, 0 } }, { '+', { 0, 2, 7, 2, 0 } }, { '=', { 0, 7, 0, 7, 0 } },
  { '/', { 1, 1, 2, 4, 4 } }, { '(', { 1, 2, 2, 2, 1 } }, { ')', { 4, 2, 2, 2, 4 } },
  { '[', { 3, 2, 2, 2, 3 } }, { ']', { 6, 2, 2, 2, 6 } }, { '<', { 1, 2, 4, 2, 1 } },
  { '>', { 4, 2, 1, 2, 4 } }, { '%', { 5, 1, 2, 4, 5 } }, { '#', { 5, 7, 5, 7, 5 } },
  { '*', { 0, 5, 2, 5, 0 } }, { '_', { 0, 0, 0, 0, 7 } }, { '\'', { 2, 2, 0, 0, 0 } },
  { '"', { 5, 5, 0, 0, 0 } },
};

/* The five rows of c (upper-cased) in the font, or NULL if it has no such character. */
static const unsigned char *glyph(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (size_t i = 0; i < sizeof FONT / sizeof FONT[0]; i++)
    if (FONT[i].c == c) return FONT[i].row;
  return NULL;
}

int rd_text_has(char c) { return glyph(c) != NULL; }

int rd_text_layout(const char *text, float x, float y, float px, rd_rect *out, int max) {
  int n = 0;
  for (int ci = 0; text[ci]; ci++) {
    const unsigned char *g = glyph(text[ci]);
    if (!g) continue;
    const float gx = x + (float)(ci * RD_TEXT_ADVANCE) * px;
    for (int r = 0; r < RD_TEXT_GLYPH_H; r++) {
      for (int c = 0; c < RD_TEXT_GLYPH_W; ) {
        if (!(g[r] & (4 >> c))) { c++; continue; }
        int start = c;
        while (c < RD_TEXT_GLYPH_W && (g[r] & (4 >> c))) c++;
        if (n == max) return n;
        out[n++] = (rd_rect){ gx + (float)start * px, y + (float)r * px,
                              (float)(c - start) * px, px };
      }
    }
  }
  return n;
}

float rd_text_width(const char *text, float px) {
  size_t len = strlen(text);
  return len ? (float)(len * RD_TEXT_ADVANCE - 1) * px : 0.0f;
}
