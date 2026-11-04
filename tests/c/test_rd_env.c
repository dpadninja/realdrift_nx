/* sources: source/bionic/rd_env.c */
/* Host unit test of rd_env (environment variables the port sets). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_env.h"

/* Boehm GC must read exactly "0": only that string disables unmapping (a "00" or "" would be
 * parsed by atoi as "keep the default"), and its unmap is what aborted the game on the console. */
static void test_gc_never_unmaps(void) {
  const char *v = rd_env_fixed("GC_UNMAP_THRESHOLD");
  CHECK(v != NULL);
  CHECK(v && !strcmp(v, "0"));
}

/* Every other name, near misses and NULL included, falls through to the real environment, so
 * HOME, TMPDIR and the GC's other switches behave as before. */
static void test_others_not_set(void) {
  CHECK(rd_env_fixed("HOME") == NULL);
  CHECK(rd_env_fixed("GC_FORCE_UNMAP_ON_GCOLLECT") == NULL);
  CHECK(rd_env_fixed("GC_UNMAP_THRESHOLDX") == NULL);
  CHECK(rd_env_fixed("gc_unmap_threshold") == NULL);
  CHECK(rd_env_fixed("") == NULL);
  CHECK(rd_env_fixed(NULL) == NULL);
}

int main(void) {
  RUN(test_gc_never_unmaps);
  RUN(test_others_not_set);
  return g_failures != 0;
}
