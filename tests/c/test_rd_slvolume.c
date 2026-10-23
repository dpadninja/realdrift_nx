/* sources: source/android/rd_slvolume.c */
/* Host unit test of rd_slvolume (the OpenSL volume and mute). MIT (see LICENSE). */
/* libs: -lm */
#include <math.h>
#include "check.h"
#include "rd_slvolume.h"

static int near(float a, float b) { return fabsf(a - b) < 1e-4f; }

/* 0 mB is full volume, -2000 mB is a tenth (-20 dB), the minimum and below are silence, and
 * out-of-range values above 0 are taken as full volume rather than amplified. */
static void test_gain(void) {
  CHECK(near(rd_slvolume_gain(0), 1.0f));
  CHECK(near(rd_slvolume_gain(-2000), 0.1f));
  CHECK(near(rd_slvolume_gain(-600), powf(10.0f, -0.3f)));
  CHECK(rd_slvolume_gain(RD_SLVOLUME_MIN_MB) == 0.0f);
  CHECK(rd_slvolume_gain(-20000) == 0.0f);
  CHECK(near(rd_slvolume_gain(32768), 1.0f));
}

/* The bug that was fixed: mute -> unmute must bring the level back. The player keeps its gain
 * while muted, so the mixer's factor is 0 only while the flag is set, and a level set during
 * mute is what plays after it. */
static void test_mute_keeps_level(void) {
  float gain = rd_slvolume_gain(-600);
  int muted = 1;
  CHECK(rd_slvolume_out(gain, muted) == 0.0f);
  muted = 0;
  CHECK(near(rd_slvolume_out(gain, muted), gain));
  muted = 1;
  gain = rd_slvolume_gain(-2000);                 /* SetVolumeLevel while muted */
  CHECK(rd_slvolume_out(gain, muted) == 0.0f);
  muted = 0;
  CHECK(near(rd_slvolume_out(gain, muted), 0.1f));
}

int main(void) {
  RUN(test_gain);
  RUN(test_mute_keeps_level);
  return g_failures != 0;
}
