/* rd_slvolume.c -- see rd_slvolume.h. MIT (see LICENSE). */
#include <math.h>
#include "rd_slvolume.h"

float rd_slvolume_gain(int32_t millibel) {
  if (millibel > 0) millibel = 0;
  if (millibel <= RD_SLVOLUME_MIN_MB) return 0.0f;
  return powf(10.0f, (float)millibel / 2000.0f);
}

float rd_slvolume_out(float gain, int muted) {
  return muted ? 0.0f : gain;
}
