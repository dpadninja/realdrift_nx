/* sources: source/android/rd_features.c */
/* Host unit test of rd_features. MIT (see LICENSE). */
#include "check.h"
#include "rd_features.h"

/* FMOD picks its OpenSL ES output only on devices with low-latency audio; without this answer
 * it chose the Java AudioTrack output, which has no sink here (run 11: silence). */
static void test_low_latency_audio_selects_opensl(void) {
  CHECK(rd_has_system_feature("android.hardware.audio.low_latency"));
}

/* Features the console lacks (pro audio, telephony), near-miss names, empty and NULL are all
 * false, so the game does not enable paths we cannot serve. */
static void test_unknown_features_are_absent(void) {
  CHECK(!rd_has_system_feature("android.hardware.audio.pro"));
  CHECK(!rd_has_system_feature("android.hardware.telephony"));
  CHECK(!rd_has_system_feature("android.hardware.audio.low_latencyX"));
  CHECK(!rd_has_system_feature(""));
  CHECK(!rd_has_system_feature(NULL));
}

int main(void) {
  RUN(test_low_latency_audio_selects_opensl);
  RUN(test_unknown_features_are_absent);
  return g_failures != 0;
}
