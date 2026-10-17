/* rd_icall.c -- see rd_icall.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_icall.h"

static const rd_icall_override *const *g_tables;
static int g_ntables;
static rd_add_icall_fn g_real;
static int g_applied;
static const rd_icall_override *g_extra[RD_ICALL_MAX_EXTRA];
static int g_nextra;

int rd_icall_add_table(const rd_icall_override *table) {
  if (!table || g_nextra == RD_ICALL_MAX_EXTRA) return -1;
  g_extra[g_nextra++] = table;
  return 0;
}

void rd_icall_setup(const rd_icall_override *const tables[], int n, rd_add_icall_fn real) {
  g_tables = tables;
  g_ntables = n;
  g_real = real;
  g_applied = 0;
}

const rd_icall_override *rd_icall_find(const rd_icall_override *table, const char *name) {
  if (!table || !name) return NULL;
  for (const rd_icall_override *o = table; o->name; o++)
    if (!strcmp(o->name, name)) return o;
  return NULL;
}

void *rd_icall_lookup(const rd_icall_override *table, const char *name) {
  const rd_icall_override *o = rd_icall_find(table, name);
  return o ? o->fn : NULL;
}

void rd_add_internal_call(const char *name, const void *fn) {
  const rd_icall_override *o = NULL;
  for (int i = 0; i < g_ntables && !o; i++) o = rd_icall_find(g_tables[i], name);
  for (int i = 0; i < g_nextra && !o; i++) o = rd_icall_find(g_extra[i], name);
  if (o) {
    g_applied++;
    if (o->orig) *o->orig = (void *)fn;   /* the engine's function, for wrappers */
  }
  if (g_real) g_real(name, o ? o->fn : fn);
}

int rd_icall_applied(void) { return g_applied; }
