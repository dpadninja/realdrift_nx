/* rd_menu.c -- see rd_menu.h. MIT (see LICENSE). */
#include <string.h>
#include <strings.h>
#include "rd_menu.h"

#define AT_END (1.0f - RD_MENU_SCROLL_EPS)

/* 1 if `word` occurs in `s` ignoring case and is not the start of one of `except`. */
static int has_word(const char *s, const char *word, const char *const except[]) {
  const size_t wl = strlen(word);
  for (const char *p = s; *p; p++) {
    if (strncasecmp(p, word, wl)) continue;
    int excluded = 0;
    for (int i = 0; except && except[i]; i++)
      if (!strncasecmp(p, except[i], strlen(except[i]))) excluded = 1;
    if (!excluded) return 1;
  }
  return 0;
}

int rd_menu_is_back_name(const char *name) {
  static const char *const BACK_EXCEPT[] = { "background", "backdrop", NULL };
  if (!name) return 0;
  return has_word(name, "back", BACK_EXCEPT) || has_word(name, "close", NULL) ||
         has_word(name, "return", NULL) || has_word(name, "cancel", NULL);
}

int rd_menu_find(const rd_ugui_target *t, int n, const char *name) {
  if (!name) return -1;
  const char *slash = strchr(name, '/');
  const char *leaf = slash ? slash + 1 : name;
  const size_t plen = slash ? (size_t)(slash - name) : 0;
  for (int i = 0; i < n; i++) {
    if (strcmp(t[i].name, leaf)) continue;
    if (slash && (strlen(t[i].parent) != plen || strncmp(t[i].parent, name, plen))) continue;
    return i;
  }
  return -1;
}

int rd_menu_is_prev_name(const char *name) {
  return name && (has_word(name, "arrowleft", NULL) || has_word(name, "prev", NULL));
}

int rd_menu_is_next_name(const char *name) {
  return name && (has_word(name, "arrowright", NULL) || has_word(name, "next", NULL));
}

int rd_menu_step_target(const rd_ugui_target *t, int n, const rd_menu_rule *rule, int step) {
  const char *named = rule ? (step < 0 ? rule->prev : rule->next) : NULL;
  const int i = rd_menu_find(t, n, named);
  if (i >= 0) return i;
  for (int k = 0; k < n; k++)
    if (step < 0 ? rd_menu_is_prev_name(t[k].name) : rd_menu_is_next_name(t[k].name)) return k;
  return -1;
}

const rd_menu_rule *rd_menu_rule_for(const rd_menu_rule *rules, int nrules,
                                     const rd_ugui_target *t, int n) {
  for (int r = 0; r < nrules; r++)
    for (int i = 0; rules[r].screen && i < n; i++)
      if (!strcmp(t[i].name, rules[r].screen) || !strcmp(t[i].parent, rules[r].screen))
        return &rules[r];
  return NULL;
}

int rd_menu_back_target(const rd_ugui_target *t, int n, const rd_menu_rule *rule) {
  if (rule && rule->back) {
    const int i = rd_menu_find(t, n, rule->back);
    if (i >= 0) return i;
  }
  int best = -1;
  for (int i = 0; i < n; i++) {
    if (!rd_menu_is_back_name(t[i].name) && !rd_menu_is_back_name(t[i].parent)) continue;
    if (best < 0 || t[i].t.r.y < t[best].t.r.y ||
        (t[i].t.r.y == t[best].t.r.y && t[i].t.r.x < t[best].t.r.x)) best = i;
  }
  return best;
}

int rd_menu_wanted(int enabled, int loading, int driving, int paused, int race_ended) {
  return enabled && !loading && (!driving || paused || race_ended);
}

uint32_t rd_menu_signature(const rd_ugui_target *t, int n) {
  uint32_t sum = (uint32_t)n * 2654435761u;
  for (int i = 0; i < n; i++) {                 /* a sum of mixed ids: order does not matter */
    uint64_t x = (uint64_t)t[i].t.id;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33;
    sum += (uint32_t)x;
  }
  return sum;
}

void rd_menu_edit_begin(rd_menu_edit *e, float value, float min, float max, int whole) {
  if (max < min) { const float t = min; min = max; max = t; }
  *e = (rd_menu_edit){ .on = 1, .orig = value, .value = value, .min = min, .max = max,
                       .step = whole ? 1.0f : (max - min) / (float)RD_MENU_SLIDER_STEPS };
}

int rd_menu_edit_delta(int dir, int direction) {
  switch (direction) {
    case 0: return dir == RD_UINAV_RIGHT ? 1 : dir == RD_UINAV_LEFT ? -1 : 0;
    case 1: return dir == RD_UINAV_LEFT ? 1 : dir == RD_UINAV_RIGHT ? -1 : 0;
    case 2: return dir == RD_UINAV_UP ? 1 : dir == RD_UINAV_DOWN ? -1 : 0;
    case 3: return dir == RD_UINAV_DOWN ? 1 : dir == RD_UINAV_UP ? -1 : 0;
    default: return 0;
  }
}

float rd_menu_edit_move(rd_menu_edit *e, int delta) {
  float v = e->value + (float)delta * e->step;
  if (v < e->min) v = e->min;
  if (v > e->max) v = e->max;
  e->value = v;
  return v;
}

float rd_menu_edit_end(rd_menu_edit *e, int cancel) {
  e->on = 0;
  return cancel ? e->orig : e->value;
}

/* Start and length of r along the axis (vertical = 1: y). */
static float lo_of(const rd_uinav_rect *r, int vertical) { return vertical ? r->y : r->x; }
static float len_of(const rd_uinav_rect *r, int vertical) { return vertical ? r->h : r->w; }

/* +1 if `dir` moves towards the far end of the axis (right, down), -1 towards the near one. */
static int sign_of(int dir) { return dir == RD_UINAV_RIGHT || dir == RD_UINAV_DOWN ? 1 : -1; }

/* Normalized position after scrolling the content by px screen pixels (> 0: towards its far
 * end), clamped to 0..1. ScrollRect's y is 1 at the top, so it falls as the list goes down. */
static float scrolled(const rd_ugui_scroll *s, int vertical, float px) {
  const float range = len_of(&s->content, vertical) - len_of(&s->view, vertical);
  float v = vertical ? s->y - px / range : s->x + px / range;
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return v;
}

/* 1 if the list can still scroll towards the side `sign` (+1 far end) of the axis. */
static int can_scroll(const rd_ugui_scroll *s, int vertical, int sign) {
  if (len_of(&s->content, vertical) - len_of(&s->view, vertical) <= 1.0f) return 0;
  const float v = vertical ? 1.0f - s->y : s->x;     /* 0 = near end shown, as for x */
  return sign > 0 ? v < AT_END : v > RD_MENU_SCROLL_EPS;
}

/* One cell along the axis: the centre distance from t[from] to the nearest other target of
 * the same list in its row / column, else t[from]'s own size. */
static float cell_pitch(const rd_ugui_target *t, int n, int from, int vertical) {
  const rd_uinav_rect *f = &t[from].t.r;
  float best = 0.0f;
  for (int i = 0; i < n; i++) {
    const rd_uinav_rect *r = &t[i].t.r;
    if (i == from || t[i].scroll != t[from].scroll) continue;
    const float across_lo = vertical ? r->x : r->y, across_hi = across_lo + (vertical ? r->w : r->h);
    const float f_lo = vertical ? f->x : f->y, f_hi = f_lo + (vertical ? f->w : f->h);
    if (across_hi <= f_lo || across_lo >= f_hi) continue;          /* another row / column */
    float d = (lo_of(r, vertical) + 0.5f * len_of(r, vertical)) -
              (lo_of(f, vertical) + 0.5f * len_of(f, vertical));
    if (d < 0.0f) d = -d;
    if (d > 1.0f && (best == 0.0f || d < best)) best = d;
  }
  return best > 0.0f ? best : len_of(f, vertical);
}

rd_menu_move rd_menu_plan_move(const rd_ugui_target *t, const rd_uinav_target *flat, int n,
                               int from, int dir, const rd_ugui_scroll *s) {
  const int next = rd_uinav_step(flat, n, from, dir);
  rd_menu_move mv = { .focus = next };
  const int vertical = dir == RD_UINAV_UP || dir == RD_UINAV_DOWN;
  if (!s || from < 0 || from >= n || !t[from].scroll || !(vertical ? s->vertical : s->horizontal))
    return mv;
  const int sign = sign_of(dir);
  if (next >= 0 && t[next].scroll == t[from].scroll) {            /* ensure it is visible */
    const rd_uinav_rect *r = &t[next].t.r;
    const float view_lo = lo_of(&s->view, vertical), view_hi = view_lo + len_of(&s->view, vertical);
    const float lo = lo_of(r, vertical), hi = lo + len_of(r, vertical);
    const float px = hi > view_hi ? hi - view_hi : lo < view_lo ? lo - view_lo : 0.0f;
    if (px * (float)sign > 0.5f && can_scroll(s, vertical, sign))
      mv = (rd_menu_move){ next, 1, vertical, scrolled(s, vertical, px), 0 };
    return mv;
  }
  if (can_scroll(s, vertical, sign)) {                            /* scroll one cell first */
    const float px = (float)sign * cell_pitch(t, n, from, vertical);
    mv = (rd_menu_move){ from, 1, vertical, scrolled(s, vertical, px), 1 };
  }
  return mv;
}

int rd_menu_load_over(int32_t before, int32_t now, uint32_t frames) {
  return now != before || frames >= RD_MENU_LOAD_MAX_FRAMES;
}
