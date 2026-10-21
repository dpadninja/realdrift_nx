/* sources: source/android/rd_gltrace.c */
/* Host unit test of rd_gltrace: one frame cut into render-target segments, averaged.
 * MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_gltrace.h"

/* A frame like the post-process one: the scene into an offscreen target, a small pass, then
 * the window. Rebinding the same framebuffer does not start a segment, so a segment is one
 * render target's work, whatever the engine's redundant binds. */
static void build(rd_gltrace_frame *f) {
  rd_gltrace_begin(f, 0);
  rd_gltrace_bind(f, 5);
  rd_gltrace_viewport(f, 1280, 720);
  rd_gltrace_clear(f, 0x4100);
  rd_gltrace_draw(f, 300);
  rd_gltrace_bind(f, 5);
  rd_gltrace_draw(f, 100);
  rd_gltrace_bind(f, 7);
  rd_gltrace_viewport(f, 256, 144);
  rd_gltrace_draw(f, 6);
  rd_gltrace_bind(f, 0);
  rd_gltrace_blit(f, 0, 720, 1280, 0);
}

static void test_segments(void) {
  rd_gltrace_frame f;
  build(&f);
  CHECK(f.n == 4 && f.seg[0].fbo == 0 && f.seg[1].fbo == 5 && f.seg[3].fbo == 0);
  CHECK(f.seg[1].draws == 2 && f.seg[1].elements == 400 && f.seg[1].clear_mask == 0x4100 && f.seg[1].vp_w == 1280);
  CHECK(f.seg[2].vp_h == 144 && f.seg[3].blits == 1 && f.seg[3].blit_w == 1280 && f.seg[3].blit_h == 720 && f.seg[3].blit_flip);
}

/* A GPU wait goes to the segment that is current: the one whose work it waited for. */
static void test_gpu(void) {
  rd_gltrace_frame f;
  rd_gltrace_begin(&f, 0);
  rd_gltrace_gpu(&f, 500);
  rd_gltrace_bind(&f, 5);
  rd_gltrace_gpu(&f, 7000);
  rd_gltrace_gpu(&f, 500);
  CHECK(f.seg[0].gpu_ns == 500 && f.seg[1].gpu_ns == 7500);
}

/* The GPU times of build()'s frame: 1, 8, 0.5 and 0.5 ms -- 10 ms in all. */
static void times(rd_gltrace_frame *f, const uint64_t ms10[4]) {
  for (int i = 0; i < 4; i++) f->seg[i].gpu_ns = ms10[i] * 100000;
}

/* Frames of the same shape are averaged; one with other render targets (a menu opening, a
 * resize) is left out rather than mixed into the wrong segments. */
static void test_average(void) {
  rd_gltrace_avg a = { 0 };
  rd_gltrace_frame f, g;
  build(&f);
  const uint64_t t[4] = { 10, 80, 5, 5 };
  times(&f, t);
  rd_gltrace_add(&a, &f);
  rd_gltrace_add(&a, &f);
  rd_gltrace_begin(&g, 0);
  rd_gltrace_add(&a, &g);
  CHECK(a.frames == 2 && a.other == 1 && a.sum.seg[1].draws == 4 && a.max_ns == 10000000 && a.over == 0);
  CHECK(rd_gltrace_total_ms(&a) > 9.99 && rd_gltrace_total_ms(&a) < 10.01);
  char b[300];
  CHECK(rd_gltrace_format_segment(&a, 1, b, sizeof b) > 0);
  CHECK(strstr(b, "fbo 5") && strstr(b, "1280x720") && strstr(b, "gpu   8.00ms  80%") && strstr(b, "draws 2.0 elements 400"));
  CHECK(strstr(b, "clears 1.0 (CD)"));
  CHECK(rd_gltrace_format_segment(&a, 3, b, sizeof b) > 0 && strstr(b, "blits 1.0 (flipped)"));
  CHECK(rd_gltrace_format_segment(&a, 0, b, sizeof b) > 0 && strstr(b, "#0  fbo 0") && strstr(b, "1.00ms  10%"));
  CHECK(rd_gltrace_format_segment(&a, 4, b, sizeof b) == -1 && rd_gltrace_format_segment(&a, 1, b, 10) == -1);
  CHECK(rd_gltrace_format_summary(&a, "full", b, sizeof b) > 0);
  CHECK(strstr(b, "full: 2 frames (1 of another shape), GPU 10.00 ms per frame, max 10.00 ms, 0 over 16.7 ms, 4 segments"));
  CHECK(rd_gltrace_format_summary(&a, "full", b, 8) == -1);
}

/* A frame whose GPU time is over one frame counts as over budget: the GPU could not keep 60
 * fps there, even when the average looks fine. */
static void test_over_budget(void) {
  rd_gltrace_avg a = { 0 };
  rd_gltrace_frame f;
  build(&f);
  const uint64_t slow[4] = { 10, 160, 5, 5 };
  times(&f, slow);
  rd_gltrace_add(&a, &f);
  CHECK(a.over == 1 && a.max_ns == 18000000);
}

/* A frame with more render targets than kept counts the rest as overflow instead of writing
 * past the table. */
static void test_overflow(void) {
  rd_gltrace_frame f;
  rd_gltrace_begin(&f, 0);
  for (uint32_t i = 1; i <= RD_GLTRACE_SEGMENTS + 3; i++) rd_gltrace_bind(&f, i);
  CHECK(f.n == RD_GLTRACE_SEGMENTS && f.overflow == 4);
}

int main(void) {
  RUN(test_segments);
  RUN(test_gpu);
  RUN(test_average);
  RUN(test_overflow);
  RUN(test_over_budget);
  return g_failures != 0;
}
