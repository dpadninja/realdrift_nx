/* sources: source/android/rd_audiowatch.c */
/* Host unit test of rd_audiowatch (debug_audio silence detection). MIT (see LICENSE). */
#include "check.h"
#include "rd_audiowatch.h"

#define RATE 48000
#define BLOCK 1024

/* Quiet blocks add up; the stretch is reported once, when it reaches the limit, and sound
 * after it reports how long it lasted. */
static void test_silence_then_back(void) {
  rd_audiowatch w = { 0 };
  double s = -1;
  int silent = 0, other = 0;
  const int blocks = RATE * RD_AUDIOWATCH_SILENT_S / BLOCK + 10;
  for (int i = 0; i < blocks; i++) {
    const int ev = rd_audiowatch_block(&w, BLOCK, 3, RATE, &s);
    if (ev == RD_AW_SILENT) silent++;
    else if (ev != RD_AW_NONE) other++;
  }
  CHECK(silent == 1 && other == 0);
  CHECK(rd_audiowatch_block(&w, BLOCK, 5000, RATE, &s) == RD_AW_BACK);
  CHECK(s >= RD_AUDIOWATCH_SILENT_S && s < RD_AUDIOWATCH_SILENT_S + 1);
  CHECK(rd_audiowatch_block(&w, BLOCK, 5000, RATE, &s) == RD_AW_NONE);
}

/* Short pauses in the sound are not reported, and sound after one is no event. */
static void test_short_pause(void) {
  rd_audiowatch w = { 0 };
  double s;
  for (int i = 0; i < RATE / BLOCK; i++) CHECK(rd_audiowatch_block(&w, BLOCK, 0, RATE, &s) == RD_AW_NONE);
  CHECK(rd_audiowatch_block(&w, BLOCK, 200, RATE, &s) == RD_AW_NONE);
  CHECK(w.quiet_frames == 0);
}

/* A device that reported no rate never triggers. */
static void test_no_rate(void) {
  rd_audiowatch w = { 0 };
  double s;
  for (int i = 0; i < 1000; i++) CHECK(rd_audiowatch_block(&w, BLOCK, 0, 0, &s) == RD_AW_NONE);
  CHECK(s == 0.0);
}

int main(void) {
  RUN(test_silence_then_back);
  RUN(test_short_pause);
  RUN(test_no_rate);
  return g_failures != 0;
}
