/* rd_uinav.c -- see rd_uinav.h. MIT (see LICENSE). */
#include <float.h>
#include "rd_uinav.h"

void rd_uinav_center(const rd_uinav_rect *r, float *x, float *y) {
  *x = r->x + r->w * 0.5f;
  *y = r->y + r->h * 0.5f;
}

/* Edges and centre of r along the move (`major`, growing in the move's direction) and across
 * it (`minor`). Turning every direction into "towards +major" keeps one set of rules. */
typedef struct { float near, far, mid, lo, hi, cross; } axes;
static axes along(const rd_uinav_rect *r, int dir) {
  float cx, cy;
  rd_uinav_center(r, &cx, &cy);
  switch (dir) {
    case RD_UINAV_RIGHT: return (axes){ r->x, r->x + r->w, cx, r->y, r->y + r->h, cy };
    case RD_UINAV_LEFT:  return (axes){ -(r->x + r->w), -r->x, -cx, r->y, r->y + r->h, cy };
    case RD_UINAV_DOWN:  return (axes){ r->y, r->y + r->h, cy, r->x, r->x + r->w, cx };
    default:             return (axes){ -(r->y + r->h), -r->y, -cy, r->x, r->x + r->w, cx };
  }
}

/* FocusFinder's candidate test: d lies in the move's direction from s. strict = 0 relaxes it
 * to "d's centre is ahead" (the fallback). */
static int is_candidate(const axes *s, const axes *d, int strict) {
  if (!strict) return d->mid - s->mid > 1.0f;
  return (s->near < d->near || s->far <= d->near) && s->far < d->far;
}

/* d overlaps the beam: the band s sweeps across when it moves (same row / column). */
static int in_beam(const axes *s, const axes *d) {
  return d->hi > s->lo && d->lo < s->hi;
}

/* 13 * major^2 + minor^2: gap to d's near edge along the move, centre offset across it. */
static float distance(const axes *s, const axes *d) {
  const float major = d->near > s->far ? d->near - s->far : 0.0f;
  const float minor = d->cross - s->cross;
  return RD_UINAV_MAJOR_WEIGHT * major * major + minor * minor;
}

int rd_uinav_step(const rd_uinav_target *t, int n, int from, int dir) {
  if (from < 0 || from >= n || dir < RD_UINAV_LEFT || dir > RD_UINAV_DOWN) return -1;
  const axes s = along(&t[from].r, dir);
  for (int strict = 1; strict >= 0; strict--) {
    int best = -1, best_beam = 0;
    float best_d = FLT_MAX;
    for (int i = 0; i < n; i++) {
      if (i == from) continue;
      const axes d = along(&t[i].r, dir);
      if (!is_candidate(&s, &d, strict)) continue;
      const int beam = in_beam(&s, &d);
      const float dist = distance(&s, &d);
      /* in the beam always beats out of it; otherwise the shorter weighted distance */
      if (best < 0 || (beam && !best_beam) || (beam == best_beam && dist < best_d)) {
        best = i;
        best_beam = beam;
        best_d = dist;
      }
    }
    if (best >= 0) return best;
  }
  return -1;
}

int rd_uinav_refocus(const rd_uinav_target *t, int n, uintptr_t id, const rd_uinav_rect *last,
                     int has_last) {
  if (n <= 0) return -1;
  for (int i = 0; i < n; i++)
    if (t[i].id == id) return i;
  int best = 0;
  float lx = 0.0f, ly = 0.0f, best_d = FLT_MAX;
  if (has_last) rd_uinav_center(last, &lx, &ly);
  for (int i = 0; i < n; i++) {
    float cx, cy;
    rd_uinav_center(&t[i].r, &cx, &cy);
    /* top-left: rows first (y weighs 4x, so a slightly lower button further left loses) */
    const float d = has_last ? (cx - lx) * (cx - lx) + (cy - ly) * (cy - ly) : 4.0f * cy + cx;
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

int rd_uinav_repeat(uint32_t frame) {
  if (frame == 1) return 1;
  return frame > RD_UINAV_REPEAT_DELAY &&
         (frame - RD_UINAV_REPEAT_DELAY) % RD_UINAV_REPEAT_EVERY == 0;
}

void rd_uinav_probe(const rd_uinav_rect *r, int i, float *x, float *y) {
  static const float FX[RD_UINAV_PROBES] = { 0.5f, 0.25f, 0.75f, 0.25f, 0.75f };
  static const float FY[RD_UINAV_PROBES] = { 0.5f, 0.25f, 0.25f, 0.75f, 0.75f };
  if (i < 0 || i >= RD_UINAV_PROBES) i = 0;
  *x = r->x + r->w * FX[i];
  *y = r->y + r->h * FY[i];
}

float rd_uinav_screen_share(const rd_uinav_rect *r, float screen_w, float screen_h) {
  return screen_w > 0.0f && screen_h > 0.0f ? r->w * r->h / (screen_w * screen_h) : 0.0f;
}

int rd_uinav_on_screen(const rd_uinav_rect *r, float screen_w, float screen_h) {
  return r->w > 1.0f && r->h > 1.0f && r->x < screen_w && r->y < screen_h &&
         r->x + r->w > 0.0f && r->y + r->h > 0.0f;
}
