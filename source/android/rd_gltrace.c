/* rd_gltrace.c -- see rd_gltrace.h. MIT (see LICENSE). */
#include <stdio.h>
#include <string.h>
#include "rd_gltrace.h"
#include "rd_platform.h"

#define BIT_COLOR   0x4000u   /* GL_COLOR_BUFFER_BIT */
#define BIT_DEPTH   0x0100u   /* GL_DEPTH_BUFFER_BIT */
#define BIT_STENCIL 0x0400u   /* GL_STENCIL_BUFFER_BIT */
#define NS_PER_MS   1e6

static rd_gltrace_segment *cur(rd_gltrace_frame *f) { return &f->seg[f->n - 1]; }

void rd_gltrace_begin(rd_gltrace_frame *f, uint32_t fbo) {
  memset(f, 0, sizeof *f);
  f->n = 1;
  f->seg[0].fbo = fbo;
}

int rd_gltrace_bind(rd_gltrace_frame *f, uint32_t fbo) {
  if (cur(f)->fbo == fbo) return -1;
  if (f->n == RD_GLTRACE_SEGMENTS) { f->overflow++; return -1; }
  rd_gltrace_segment *s = &f->seg[f->n++];
  memset(s, 0, sizeof *s);
  s->fbo = fbo;
  return f->n - 1;
}

void rd_gltrace_viewport(rd_gltrace_frame *f, int w, int h) {
  cur(f)->vp_w = w;
  cur(f)->vp_h = h;
}

void rd_gltrace_draw(rd_gltrace_frame *f, uint64_t elements) {
  cur(f)->draws++;
  cur(f)->elements += elements;
}

void rd_gltrace_clear(rd_gltrace_frame *f, uint32_t mask) {
  cur(f)->clears++;
  cur(f)->clear_mask |= mask;
}

void rd_gltrace_blit(rd_gltrace_frame *f, int dx0, int dy0, int dx1, int dy1) {
  rd_gltrace_segment *s = cur(f);
  s->blits++;
  s->blit_w = dx1 > dx0 ? dx1 - dx0 : dx0 - dx1;
  s->blit_h = dy1 > dy0 ? dy1 - dy0 : dy0 - dy1;
  s->blit_flip = dy1 < dy0;
}

void rd_gltrace_copy(rd_gltrace_frame *f) { cur(f)->copies++; }
void rd_gltrace_read(rd_gltrace_frame *f) { cur(f)->reads++; }
void rd_gltrace_invalidate(rd_gltrace_frame *f) { cur(f)->invalidates++; }

void rd_gltrace_gpu(rd_gltrace_frame *f, uint64_t ns) { cur(f)->gpu_ns += ns; }

/* 1 if b has a's segments: the same number, into the same framebuffers. */
static int same_shape(const rd_gltrace_frame *a, const rd_gltrace_frame *b) {
  if (a->n != b->n) return 0;
  for (int i = 0; i < a->n; i++)
    if (a->seg[i].fbo != b->seg[i].fbo) return 0;
  return 1;
}

/* A frame's GPU time: its segments together. */
static uint64_t frame_ns(const rd_gltrace_frame *f) {
  uint64_t ns = 0;
  for (int i = 0; i < f->n; i++) ns += f->seg[i].gpu_ns;
  return ns;
}

void rd_gltrace_add(rd_gltrace_avg *a, const rd_gltrace_frame *f) {
  if (a->frames && !same_shape(&a->sum, f)) { a->other++; return; }
  const uint64_t ns = frame_ns(f);
  if (ns > a->max_ns) a->max_ns = ns;
  a->over += ns > RD_FRAME_NS;
  if (!a->frames) {
    a->sum = *f;
    a->frames = 1;
    return;
  }
  a->frames++;
  for (int i = 0; i < f->n; i++) {
    rd_gltrace_segment *s = &a->sum.seg[i];
    const rd_gltrace_segment *t = &f->seg[i];
    s->draws += t->draws;
    s->clears += t->clears;
    s->blits += t->blits;
    s->copies += t->copies;
    s->reads += t->reads;
    s->invalidates += t->invalidates;
    s->elements += t->elements;
    s->clear_mask |= t->clear_mask;
    s->gpu_ns += t->gpu_ns;
  }
}

double rd_gltrace_total_ms(const rd_gltrace_avg *a) {
  return a->frames ? (double)frame_ns(&a->sum) / a->frames / NS_PER_MS : 0;
}

int rd_gltrace_format_summary(const rd_gltrace_avg *a, const char *label, char *buf, size_t n) {
  const int k = snprintf(buf, n,
                         "%s: %d frames (%d of another shape), GPU %.2f ms per frame, max %.2f ms, "
                         "%d over %.1f ms, %d segments%s",
                         label, a->frames, a->other, rd_gltrace_total_ms(a), (double)a->max_ns / NS_PER_MS,
                         a->over, (double)RD_FRAME_NS / NS_PER_MS, a->sum.n,
                         a->sum.overflow ? " (more did not fit)" : "");
  return k < 0 || (size_t)k >= n ? -1 : k;
}

int rd_gltrace_format_segment(const rd_gltrace_avg *a, int i, char *buf, size_t n) {
  if (!a->frames || i < 0 || i >= a->sum.n) return -1;
  const rd_gltrace_segment *s = &a->sum.seg[i];
  const double fr = a->frames, ms = (double)s->gpu_ns / fr / NS_PER_MS, total = rd_gltrace_total_ms(a);
  char clear[4] = "-";
  int c = 0;
  if (s->clear_mask & BIT_COLOR) clear[c++] = 'C';
  if (s->clear_mask & BIT_DEPTH) clear[c++] = 'D';
  if (s->clear_mask & BIT_STENCIL) clear[c++] = 'S';
  if (c) clear[c] = 0;
  const int k = snprintf(buf, n,
                         "#%-2d fbo %-3u %4dx%-4d gpu %6.2fms %3.0f%%  draws %.1f elements %.0f "
                         "clears %.1f (%s) blits %.1f%s copies %.1f reads %.1f invalidates %.1f",
                         i, s->fbo, s->vp_w, s->vp_h, ms, total > 0 ? 100.0 * ms / total : 0,
                         s->draws / fr, (double)s->elements / fr, s->clears / fr, clear, s->blits / fr,
                         s->blits ? (s->blit_flip ? " (flipped)" : "") : "", s->copies / fr, s->reads / fr,
                         s->invalidates / fr);
  return k < 0 || (size_t)k >= n ? -1 : k;
}
