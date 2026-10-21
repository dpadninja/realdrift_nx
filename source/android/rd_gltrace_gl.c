/* rd_gltrace_gl.c -- the GL side of the frame trace (rd_gltrace.h): wrappers of the engine's
 * render-target calls, and the GPU wait at the end of each segment. MIT (see LICENSE).
 *
 * Only with config.txt debug_gl_trace = 1: egl_GetProcAddress_timed / egl_wrap_gl hand the
 * engine these wrappers instead of mesa's functions. They always follow the bound draw
 * framebuffer (one compare); while a capture runs they also record the frame (rd_gltrace.c) and,
 * when the draw framebuffer changes and at the swap, wait for the GPU (glFinish) and count the
 * wait as the segment's GPU time. A captured frame starts with an idle GPU, so the waits add up
 * to its GPU time; it runs slower than a normal frame.
 * Everything here runs on the engine's GL thread; rd_gltrace_arm may come from any thread. */
#include <string.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include "rd_gltrace.h"
#include "rd_gltrace_gl.h"
#include "rd_platform.h"
#include "util.h"

#define TAG "[gltrace]"

/* The engine's functions, as mesa gives them. */
static PFNGLBINDFRAMEBUFFERPROC     real_bind;
static PFNGLVIEWPORTPROC            real_viewport;
static PFNGLCLEARPROC               real_clear;
static PFNGLDRAWARRAYSPROC          real_draw_arrays;
static PFNGLDRAWELEMENTSPROC        real_draw_elements;
static PFNGLDRAWARRAYSINSTANCEDPROC real_draw_arrays_inst;
static PFNGLDRAWELEMENTSINSTANCEDPROC real_draw_elements_inst;
static PFNGLDRAWRANGEELEMENTSPROC   real_draw_range;
static PFNGLBLITFRAMEBUFFERPROC     real_blit;
static PFNGLCOPYTEXSUBIMAGE2DPROC   real_copy_sub;
static PFNGLCOPYTEXIMAGE2DPROC      real_copy;
static PFNGLREADPIXELSPROC          real_read;
static PFNGLINVALIDATEFRAMEBUFFERPROC real_invalidate;
static PFNGLDISCARDFRAMEBUFFEREXTPROC real_discard;

static GLuint g_fbo;                               /* the bound draw framebuffer */
static const char *volatile g_armed;               /* label of the next capture */
static const char *g_label;                        /* label of the running capture */
static int g_recording;                            /* 1 while a frame is recorded */
static int g_frames;                               /* frames recorded this capture */
static rd_gltrace_frame g_frame;
static rd_gltrace_avg g_avg;

/* Waits until the GPU has done everything issued so far; the wait goes to the current segment. */
static void finish_segment(void) {
  const uint64_t t0 = rd_now_ns();
  glFinish();
  rd_gltrace_gpu(&g_frame, rd_now_ns() - t0);
}

/* Logs the finished capture and resets it. */
static void report(void) {
  char line[320];
  if (rd_gltrace_format_summary(&g_avg, g_label, line, sizeof line) > 0) debugPrintf(TAG " %s\n", line);
  for (int i = 0; i < g_avg.sum.n; i++)
    if (rd_gltrace_format_segment(&g_avg, i, line, sizeof line) > 0) debugPrintf(TAG "   %s\n", line);
  memset(&g_avg, 0, sizeof g_avg);
  g_label = NULL;
}

void rd_gltrace_arm(const char *label) { g_armed = label; }

void rd_gltrace_frame_end(void) {
  if (g_recording) {                               /* the recorded frame ends at the swap */
    finish_segment();
    rd_gltrace_add(&g_avg, &g_frame);
    g_recording = 0;
    if (++g_frames >= RD_GLTRACE_FRAMES) report();
  }
  if (!g_label && g_armed) {                       /* a new capture */
    g_label = (const char *)g_armed;
    g_armed = NULL;
    g_frames = 0;
  }
  if (!g_label) return;
  glFinish();                                      /* the next frame starts with an idle GPU */
  rd_gltrace_begin(&g_frame, g_fbo);
  g_recording = 1;
}

/* ---- wrappers ---- */

static void w_bind(GLenum target, GLuint fbo) {
  if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
    g_fbo = fbo;
    if (g_recording && fbo != g_frame.seg[g_frame.n - 1].fbo) {   /* a new segment */
      finish_segment();
      rd_gltrace_bind(&g_frame, fbo);
    }
  }
  real_bind(target, fbo);
}
static void w_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
  if (g_recording) rd_gltrace_viewport(&g_frame, w, h);
  real_viewport(x, y, w, h);
}
static void w_clear(GLbitfield mask) {
  if (g_recording) rd_gltrace_clear(&g_frame, mask);
  real_clear(mask);
}
static void w_draw_arrays(GLenum m, GLint first, GLsizei n) {
  if (g_recording) rd_gltrace_draw(&g_frame, (uint64_t)n);
  real_draw_arrays(m, first, n);
}
static void w_draw_elements(GLenum m, GLsizei n, GLenum t, const void *i) {
  if (g_recording) rd_gltrace_draw(&g_frame, (uint64_t)n);
  real_draw_elements(m, n, t, i);
}
static void w_draw_arrays_inst(GLenum m, GLint first, GLsizei n, GLsizei inst) {
  if (g_recording) rd_gltrace_draw(&g_frame, (uint64_t)n * (uint64_t)inst);
  real_draw_arrays_inst(m, first, n, inst);
}
static void w_draw_elements_inst(GLenum m, GLsizei n, GLenum t, const void *i, GLsizei inst) {
  if (g_recording) rd_gltrace_draw(&g_frame, (uint64_t)n * (uint64_t)inst);
  real_draw_elements_inst(m, n, t, i, inst);
}
static void w_draw_range(GLenum m, GLuint s, GLuint e, GLsizei n, GLenum t, const void *i) {
  if (g_recording) rd_gltrace_draw(&g_frame, (uint64_t)n);
  real_draw_range(m, s, e, n, t, i);
}
static void w_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,
                   GLint dy1, GLbitfield mask, GLenum filter) {
  if (g_recording) rd_gltrace_blit(&g_frame, dx0, dy0, dx1, dy1);
  real_blit(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}
static void w_copy_sub(GLenum t, GLint l, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h) {
  if (g_recording) rd_gltrace_copy(&g_frame);
  real_copy_sub(t, l, xo, yo, x, y, w, h);
}
static void w_copy(GLenum t, GLint l, GLenum f, GLint x, GLint y, GLsizei w, GLsizei h, GLint b) {
  if (g_recording) rd_gltrace_copy(&g_frame);
  real_copy(t, l, f, x, y, w, h, b);
}
static void w_read(GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void *p) {
  if (g_recording) rd_gltrace_read(&g_frame);
  real_read(x, y, w, h, f, t, p);
}
static void w_invalidate(GLenum t, GLsizei n, const GLenum *a) {
  if (g_recording) rd_gltrace_invalidate(&g_frame);
  real_invalidate(t, n, a);
}
static void w_discard(GLenum t, GLsizei n, const GLenum *a) {
  if (g_recording) rd_gltrace_invalidate(&g_frame);
  real_discard(t, n, a);
}

void *rd_gltrace_wrap(const char *name, void *real) {
#define WRAP(gl, var, w) if (!strcmp(name, gl)) { var = real; return (void *)&w; }
  WRAP("glBindFramebuffer", real_bind, w_bind)
  WRAP("glViewport", real_viewport, w_viewport)
  WRAP("glClear", real_clear, w_clear)
  WRAP("glDrawArrays", real_draw_arrays, w_draw_arrays)
  WRAP("glDrawElements", real_draw_elements, w_draw_elements)
  WRAP("glDrawArraysInstanced", real_draw_arrays_inst, w_draw_arrays_inst)
  WRAP("glDrawElementsInstanced", real_draw_elements_inst, w_draw_elements_inst)
  WRAP("glDrawRangeElements", real_draw_range, w_draw_range)
  WRAP("glBlitFramebuffer", real_blit, w_blit)
  WRAP("glCopyTexSubImage2D", real_copy_sub, w_copy_sub)
  WRAP("glCopyTexImage2D", real_copy, w_copy)
  WRAP("glReadPixels", real_read, w_read)
  WRAP("glInvalidateFramebuffer", real_invalidate, w_invalidate)
  WRAP("glDiscardFramebufferEXT", real_discard, w_discard)
#undef WRAP
  return NULL;
}
