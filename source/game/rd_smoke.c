/* rd_smoke.c -- see rd_smoke.h. MIT (see LICENSE). */
#include "rd_smoke.h"

int rd_smoke_active(int size, int amount) {
  return size < RD_SMOKE_PERCENT_ORIG || amount < RD_SMOKE_PERCENT_ORIG;
}

int rd_smoke_keep(int *acc, int amount) {
  *acc += amount;
  if (*acc < RD_SMOKE_PERCENT_ORIG) return 0;
  *acc -= RD_SMOKE_PERCENT_ORIG;
  return 1;
}

int rd_smoke_emit_allowed(float time_scale) {
  return time_scale > 0.0f;
}

float rd_smoke_scale_size(float size, int percent) {
  return size * (float)percent / (float)RD_SMOKE_PERCENT_ORIG;
}

int rd_smoke_set_has(const rd_smoke_set *s, const void *go) {
  for (int i = 0; i < s->n; i++)
    if (s->go[i] == go) return 1;
  return 0;
}

void rd_smoke_set_add(rd_smoke_set *s, const void *go) {
  if (!go || s->n >= RD_SMOKE_MAX_SYSTEMS || rd_smoke_set_has(s, go)) return;
  s->go[s->n++] = go;
}
