/* rd_modhost.c -- see rd_modhost.h. MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include "rd_modhost.h"

static const rd_mod_info *g_sort_infos;   /* qsort has no context argument */

/* qsort order: priority descending, then id ascending (deterministic between runs). */
static int cmp_order(const void *a, const void *b) {
  const rd_mod_info *x = &g_sort_infos[*(const int *)a], *y = &g_sort_infos[*(const int *)b];
  if (x->priority != y->priority) return x->priority > y->priority ? -1 : 1;
  return strcmp(x->id, y->id);
}

void rd_mods_order(const rd_mod_info *infos, int *order, int n) {
  for (int i = 0; i < n; i++) order[i] = i;
  g_sort_infos = infos;
  qsort(order, (size_t)n, sizeof *order, cmp_order);
  g_sort_infos = NULL;
}

int rd_claim_take(rd_claims *t, const char *key, int owner) {
  for (int i = 0; i < t->n; i++)
    if (!strcmp(t->c[i].key, key)) return t->c[i].owner;
  if (t->n == t->max) return -2;
  t->c[t->n++] = (rd_claim){ key, owner };
  return -1;
}

int rd_modtext_add(rd_modtext *q, float x, float y, float size, uint32_t rgba, const char *text) {
  if (!text || q->npending == RD_MODTEXT_MAX) return -1;
  rd_modtext_line *l = &q->pending[q->npending++];
  l->x = x; l->y = y; l->size = size; l->rgba = rgba;
  strncpy(l->text, text, sizeof l->text - 1);
  l->text[sizeof l->text - 1] = 0;
  return 0;
}

void rd_modtext_swap(rd_modtext *q) {
  memcpy(q->shown, q->pending, sizeof(rd_modtext_line) * (size_t)q->npending);
  q->nshown = q->npending;
  q->npending = 0;
}
