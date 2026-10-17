/* rd_features.c -- see rd_features.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_features.h"

static const char *const FEATURES[] = {
  "android.hardware.audio.low_latency",
  "android.hardware.audio.output",
  "android.hardware.touchscreen",
  "android.hardware.touchscreen.multitouch",
  "android.hardware.gamepad",
  NULL,
};

int rd_has_system_feature(const char *name) {
  if (!name) return 0;
  for (int i = 0; FEATURES[i]; i++)
    if (!strcmp(FEATURES[i], name)) return 1;
  return 0;
}
