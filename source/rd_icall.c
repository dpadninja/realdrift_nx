/* rd_icall.c -- see rd_icall.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_icall.h"

static const rd_icall_override *g_table;
static rd_add_icall_fn g_real;
static int g_applied;

void rd_icall_setup(const rd_icall_override *table, rd_add_icall_fn real) {
  g_table = table;
  g_real = real;
  g_applied = 0;
}

void *rd_icall_lookup(const rd_icall_override *table, const char *name) {
  if (!table || !name) return NULL;
  for (const rd_icall_override *o = table; o->name; o++)
    if (!strcmp(o->name, name)) return o->fn;
  return NULL;
}

void rd_add_internal_call(const char *name, const void *fn) {
  void *ours = rd_icall_lookup(g_table, name);
  if (ours) g_applied++;
  if (g_real) g_real(name, ours ? ours : fn);
}

int rd_icall_applied(void) { return g_applied; }
