/* rd_menu.c -- see rd_menu.h. MIT (see LICENSE). */
#include <string.h>
#include <strings.h>
#include "rd_menu.h"

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
