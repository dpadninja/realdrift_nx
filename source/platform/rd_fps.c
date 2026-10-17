/* rd_fps.c -- see rd_fps.h. MIT (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include "rd_fps.h"
#include "rd_platform.h"
#include "rd_text.h"

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

int rd_fps_layout(const char *text, float x, float y, float px, rd_rect *out, int max) {
  return rd_text_layout(text, x, y, px, out, max);
}

float rd_fps_text_width(const char *text, float px) { return rd_text_width(text, px); }
