/* rd_audiowatch.c -- see rd_audiowatch.h. MIT (see LICENSE). */
#include "rd_audiowatch.h"

int rd_audiowatch_block(rd_audiowatch *w, int frames, int peak, int rate, double *secs) {
  const double quiet_s = rate > 0 ? (double)w->quiet_frames / rate : 0.0;
  *secs = quiet_s;
  if (peak > RD_AUDIOWATCH_PEAK) {
    const int was_reported = w->reported;
    w->quiet_frames = 0;
    w->reported = 0;
    return was_reported ? RD_AW_BACK : RD_AW_NONE;
  }
  if (frames > 0) w->quiet_frames += (uint64_t)frames;
  *secs = rate > 0 ? (double)w->quiet_frames / rate : 0.0;
  if (!w->reported && rate > 0 && w->quiet_frames >= (uint64_t)rate * RD_AUDIOWATCH_SILENT_S) {
    w->reported = 1;
    return RD_AW_SILENT;
  }
  return RD_AW_NONE;
}
