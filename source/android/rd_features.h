/* rd_features.h -- PackageManager.hasSystemFeature() answers. Host-testable
 * (tests/c/test_rd_features.c).
 *
 * Unity 2018.4 picks FMOD's Android output by asking hasSystemFeature(
 * "android.hardware.audio.low_latency") (after isBluetoothA2dpOn()). The inherited fake JNI
 * answered false, so FMOD chose the Java AudioTrack output, which has no working sink here
 * (no JVM run() loop) -> silence (run 11). Claiming low-latency audio makes FMOD use
 * OpenSL ES, which opensles.c implements on top of SDL audio.
 * MIT (see LICENSE). */
#ifndef RD_FEATURES_H
#define RD_FEATURES_H

/* PackageManager.hasSystemFeature(name): 1 for the features the port has (audio output and
 * low-latency audio for FMOD's OpenSL output, touch screen and multitouch, gamepad), else 0. */
int rd_has_system_feature(const char *name);

#endif
