/* rd_overlay.c -- the GL-free part of rd_overlay.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_overlay.h"

int rd_overlay_rect_tris(const rd_rect *r, int n, float *out, int max) {
  if (n > max) n = max;
  for (int i = 0; i < n; i++) {
    const float x0 = r[i].x, y0 = r[i].y, x1 = x0 + r[i].w, y1 = y0 + r[i].h;
    const float q[RD_OVERLAY_RECT_FLOATS] = { x0,y0, x1,y0, x0,y1,  x1,y0, x1,y1, x0,y1 };
    memcpy(out + i * RD_OVERLAY_RECT_FLOATS, q, sizeof q);
  }
  return n < 0 ? 0 : n;
}

static rd_overlay_draw_fn g_mod_draw;
void rd_overlay_set_mod_draw(rd_overlay_draw_fn fn) { g_mod_draw = fn; }
rd_overlay_draw_fn rd_overlay_mod_draw(void) { return g_mod_draw; }
