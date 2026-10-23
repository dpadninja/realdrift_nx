/* rd_audioout.h -- the sound output: libnx audout fed by one thread of ours, in place of SDL's
 * Switch audio backend.
 *
 * SDL 2.28.5's SWITCHAUDIO_PlayDevice waits for a just-queued buffer to reach "playing"; when
 * its thread is kept off the CPU longer than a buffer (core 0 during a load), the buffer is
 * already "done", the wait never ends and the sound is gone until restart
 * (analysis/research/26). audout hands played buffers back itself, so there is no state to
 * miss: the thread waits for a released buffer, fills it and queues it again. S16 stereo at
 * audout's rate (48 kHz). No host test: a loop over libnx audout calls; checked on the console
 * (debug_audio). MIT (see LICENSE). */
#ifndef RD_AUDIOOUT_H
#define RD_AUDIOOUT_H

#include <stdint.h>

/* Fills `bytes` bytes of S16 stereo samples at `out`. Called on the output thread. */
typedef void (*rd_audioout_fill)(void *ctx, uint8_t *out, int bytes);

/* Starts audout and the output thread, which calls fill(ctx, ...) for every buffer. Returns
 * the sample rate, or 0 on failure (logged; no sound then). Call once; any thread. */
int rd_audioout_open(rd_audioout_fill fill, void *ctx);

/* Stops the thread and audout (no-op when not open). Any thread but the output thread. */
void rd_audioout_close(void);

#endif
