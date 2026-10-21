/* rd_gltrace.h -- debug trace of what one frame does on the GPU (config.txt debug_gl_trace).
 *
 * The engine's GL calls that change or fill render targets are wrapped (rd_gltrace_gl.c) while
 * a capture runs. A frame is cut into segments at every change of the draw framebuffer; each
 * segment counts its draws, clears, blits, copies, reads and invalidates and keeps its viewport.
 * At the end of a segment the CPU waits for the GPU (glFinish): that wait is the segment's GPU
 * time, on the CPU clock. (GL_TIMESTAMP was tried: on mesa/Switch it runs at 0.6x and its
 * split between segments did not match the frame times.) A capture averages RD_GLTRACE_FRAMES
 * frames of the same shape and logs one line per segment: where the GPU time of a frame goes,
 * e.g. the scene into an offscreen target, then the post-process passes. The waits make the
 * captured frames slower; only for debugging.
 * Pure logic here (host-tested, tests/c/test_rd_gltrace.c). MIT (see LICENSE). */
#ifndef RD_GLTRACE_H
#define RD_GLTRACE_H

#include <stddef.h>
#include <stdint.h>

#define RD_GLTRACE_SEGMENTS 40   /* segments kept per frame; more are counted as overflow */

/* One segment: the work drawn into one framebuffer between two binds. */
typedef struct {
  uint32_t fbo;                /* draw framebuffer (0 = the window) */
  int      vp_w, vp_h;         /* the last viewport set in it */
  uint32_t draws, clears, blits, copies, reads, invalidates;
  uint64_t elements;           /* vertices / indices drawn */
  uint32_t clear_mask;         /* GL_*_BUFFER_BIT of its clears, or-ed */
  int      blit_w, blit_h, blit_flip;   /* the last blit's destination size; flip: y reversed */
  uint64_t gpu_ns;             /* filled by rd_gltrace_set_gpu */
} rd_gltrace_segment;

/* The segments of one frame. */
typedef struct {
  rd_gltrace_segment seg[RD_GLTRACE_SEGMENTS];
  int n;
  int overflow;                /* segments that did not fit */
} rd_gltrace_frame;

/* Starts an empty frame whose first segment draws into `fbo` (the one bound at the start). */
void rd_gltrace_begin(rd_gltrace_frame *f, uint32_t fbo);

/* The draw framebuffer was bound to fbo: a new segment when it differs from the current one.
 * Returns the new segment's index, or -1 when it is the same framebuffer or no room is left. */
int rd_gltrace_bind(rd_gltrace_frame *f, uint32_t fbo);

/* Events of the current segment. */
void rd_gltrace_viewport(rd_gltrace_frame *f, int w, int h);
void rd_gltrace_draw(rd_gltrace_frame *f, uint64_t elements);
void rd_gltrace_clear(rd_gltrace_frame *f, uint32_t mask);
void rd_gltrace_blit(rd_gltrace_frame *f, int dx0, int dy0, int dx1, int dy1);
void rd_gltrace_copy(rd_gltrace_frame *f);
void rd_gltrace_read(rd_gltrace_frame *f);
void rd_gltrace_invalidate(rd_gltrace_frame *f);

/* The GPU time of the current segment (the wait at its end). */
void rd_gltrace_gpu(rd_gltrace_frame *f, uint64_t ns);

/* Averages of several frames of the same shape. */
typedef struct {
  rd_gltrace_frame sum;        /* the first frame's shape; counts and gpu_ns summed */
  int frames;                  /* frames summed */
  int other;                   /* frames of another shape, left out */
  uint64_t max_ns;             /* the slowest summed frame's GPU time */
  int over;                    /* summed frames whose GPU time is over one frame (16.7 ms) */
} rd_gltrace_avg;

/* Adds frame f: the first sets the shape; a frame with other segments (count or framebuffers)
 * only counts in `other`. */
void rd_gltrace_add(rd_gltrace_avg *a, const rd_gltrace_frame *f);

/* Segment i of the average as one log line into buf (n bytes): framebuffer, viewport, GPU ms,
 * share of the frame's GPU time, and the per-frame counts. Returns its length, or -1. */
int rd_gltrace_format_segment(const rd_gltrace_avg *a, int i, char *buf, size_t n);

/* The frame's total GPU ms per frame of the average (0 without frames). */
double rd_gltrace_total_ms(const rd_gltrace_avg *a);

/* The capture's summary line (without the tag) into buf: label, frames, GPU ms average and
 * max, frames over one frame's time, segments. Its length, or -1. */
int rd_gltrace_format_summary(const rd_gltrace_avg *a, const char *label, char *buf, size_t n);

#endif
