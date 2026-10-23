/* rd_audiowatch.h -- config.txt debug_audio: notices when the sound output goes quiet and when it
 * comes back, so a lost sound shows up in debug.log with the state of the OpenSL players at
 * that moment (opensles_mixer.c logs it). The SDL audio callback feeds every device block.
 * Pure logic, host-tested (tests/c/test_rd_audiowatch.c). MIT (see LICENSE). */
#ifndef RD_AUDIOWATCH_H
#define RD_AUDIOWATCH_H

#include <stdint.h>

#define RD_AUDIOWATCH_PEAK      64     /* a block louder than this is sound */
#define RD_AUDIOWATCH_SILENT_S  3      /* quiet this long is reported */

enum { RD_AW_NONE, RD_AW_SILENT, RD_AW_BACK };

typedef struct {
  uint64_t quiet_frames;   /* device frames since the last block with sound */
  int reported;            /* the current quiet stretch was reported */
} rd_audiowatch;

/* One device block of `frames` frames at `rate` Hz whose loudest sample was `peak`: returns
 * RD_AW_SILENT once when a quiet stretch reaches RD_AUDIOWATCH_SILENT_S, RD_AW_BACK when sound
 * returns after a reported stretch (*secs = how long it was quiet), else RD_AW_NONE. */
int rd_audiowatch_block(rd_audiowatch *w, int frames, int peak, int rate, double *secs);

#endif
