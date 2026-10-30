/* sources: source/android/rd_eglcfg.c */
/* Host unit test of rd_eglcfg. MIT (see LICENSE). */
#include "check.h"
#include "rd_eglcfg.h"

#define EGL_RED_SIZE   0x3024
#define EGL_DEPTH_SIZE 0x3025

/* Present sample attributes are replaced (not doubled), the rest is kept in order, and the
 * multisample request is appended before EGL_NONE. */
static void test_samples_replaced(void) {
  const int32_t in[] = { EGL_RED_SIZE, 8, RD_EGL_SAMPLES, 0, EGL_DEPTH_SIZE, 24,
                         RD_EGL_SAMPLE_BUFFERS, 0, RD_EGL_NONE };
  int32_t out[RD_EGL_ATTRIBS_MAX];
  CHECK(rd_egl_attribs_samples(in, out, RD_EGL_ATTRIBS_MAX, 4) == 9);
  const int32_t want[] = { EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, RD_EGL_SAMPLE_BUFFERS, 1,
                           RD_EGL_SAMPLES, 4, RD_EGL_NONE };
  for (int i = 0; i < 9; i++) CHECK(out[i] == want[i]);
  CHECK(rd_egl_attrib(out, RD_EGL_SAMPLES, -1) == 4);
  CHECK(rd_egl_attrib(in, RD_EGL_SAMPLES, -1) == 0);
}

/* A NULL list (EGL's "any config") still gets the request; a list that does not fit the
 * buffer is refused rather than cut. */
static void test_null_and_overflow(void) {
  int32_t out[RD_EGL_ATTRIBS_MAX];
  CHECK(rd_egl_attribs_samples(NULL, out, RD_EGL_ATTRIBS_MAX, 2) == 5 && out[3] == 2 &&
        out[4] == RD_EGL_NONE);
  CHECK(rd_egl_attrib(NULL, RD_EGL_SAMPLES, 7) == 7);
  const int32_t in[] = { EGL_RED_SIZE, 8, EGL_DEPTH_SIZE, 24, RD_EGL_NONE };
  CHECK(rd_egl_attribs_samples(in, out, 8, 4) == 0);
  CHECK(rd_egl_attribs_samples(in, out, 9, 4) == 9);
  CHECK(rd_egl_attribs_samples(in, out, 3, 4) == 0);
}

int main(void) {
  RUN(test_samples_replaced);
  RUN(test_null_and_overflow);
  return g_failures != 0;
}
