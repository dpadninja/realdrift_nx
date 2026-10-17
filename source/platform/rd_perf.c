/* rd_perf.c -- see rd_perf.h. MIT (see LICENSE). */
#include "rd_perf.h"

void rd_pick_resolution(int setting, int docked, int *w, int *h) {
  int big = setting == RD_RES_1080 || (setting != RD_RES_720 && docked);
  *w = big ? RD_DOCKED_W : RD_HANDHELD_W;
  *h = big ? RD_DOCKED_H : RD_HANDHELD_H;
}

int rd_boost_update(rd_boost *b, uint64_t now_ns, int in_frame, uint64_t frame_start_ns,
                    uint64_t threshold_ns, uint64_t tail_ns) {
  int long_frame = in_frame && now_ns - frame_start_ns > threshold_ns;
  if (long_frame) b->until_ns = now_ns + tail_ns;
  b->on = long_frame || (b->until_ns && now_ns < b->until_ns);
  return b->on;
}
