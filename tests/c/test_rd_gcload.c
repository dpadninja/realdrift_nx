/* sources: source/engine/rd_gcload.c */
/* Host unit test of rd_gcload. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_gcload.h"

/* A fake Boehm disable count and a trace of the calls. */
static int g_disabled;
static char g_trace[64];
static int g_collected_while_off;
static void note(char c) { size_t n = strlen(g_trace); g_trace[n] = c; g_trace[n + 1] = 0; }
static int f_is_disabled(void) { note('?'); return g_disabled > 0; }
static void f_enable(void) { note('+'); g_disabled--; }
static void f_disable(void) { note('-'); g_disabled++; }
static void f_collect(int gen) { (void)gen; note('C'); if (g_disabled > 0) g_collected_while_off++; }
static const rd_gc_ops OPS = { f_is_disabled, f_enable, f_disable, f_collect };

static void reset(int disabled) { g_disabled = disabled; g_trace[0] = 0; g_collected_while_off = 0; }

/* The port's own disable (count 1): the GC is turned on for the collection and off again, so
 * the engine's request is honoured and play stays without automatic collections. */
static void test_off_by_the_port(void) {
  reset(1);
  CHECK(rd_gcload_collect(&OPS, 0) == RD_GC_RAN_TURNED_ON);
  CHECK(!strcmp(g_trace, "?+?C-"));
  CHECK(g_disabled == 1 && g_collected_while_off == 0);
}

/* GC already on: collect only, the count is not touched. */
static void test_already_on(void) {
  reset(0);
  CHECK(rd_gcload_collect(&OPS, 0) == RD_GC_RAN);
  CHECK(!strcmp(g_trace, "?C"));
  CHECK(g_disabled == 0);
}

/* Another disable held on top of ours (count 2): one enable is not enough; the request is
 * passed on as a no-op and reported, and the count is restored -- the port never undoes a
 * disable it did not make. */
static void test_held_by_someone_else(void) {
  reset(2);
  CHECK(rd_gcload_collect(&OPS, 0) == RD_GC_STILL_OFF);
  CHECK(g_disabled == 2);
}

int main(void) {
  RUN(test_off_by_the_port);
  RUN(test_already_on);
  RUN(test_held_by_someone_else);
  return g_failures != 0;
}
