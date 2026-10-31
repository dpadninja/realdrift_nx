/* sources: source/engine/rd_gcload.c */
/* Host unit test of rd_gcload. MIT (see LICENSE). */
#include "check.h"
#include "rd_gcload.h"
#include "rd_platform.h"

/* A scene load ("Race again" took 0.8-0.95 s, the garage 2-3 s) with the engine's
 * stop-the-world (UnloadUnusedAssets) is a load to collect after. */
static void test_load_with_stop(void) {
  CHECK(rd_gcload_due(RD_GC_LOAD_FRAME_NS, 1));
  CHECK(rd_gcload_due(915 * RD_NS_PER_MS, 6));
  CHECK(rd_gcload_due(3 * RD_NS_PER_S, 6));
}

/* A short frame never collects, even with a stop (the engine stopped the world in a 100 ms
 * menu frame): a race must not lose frames to the GC. */
static void test_short_frame(void) {
  CHECK(!rd_gcload_due(100 * RD_NS_PER_MS, 2));
  CHECK(!rd_gcload_due(RD_GC_LOAD_FRAME_NS - 1, 6));
}

/* A long frame without a stop is not a scene load (menu hitches of 0.4-0.5 s read files). */
static void test_long_frame_without_stop(void) {
  CHECK(!rd_gcload_due(556 * RD_NS_PER_MS, 0));
}

int main(void) {
  RUN(test_load_with_stop);
  RUN(test_short_frame);
  RUN(test_long_frame_without_stop);
  return g_failures != 0;
}
