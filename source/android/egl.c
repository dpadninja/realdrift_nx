/* egl.c -- EGL/GLES glue between the engine and switch-mesa: the window surface kept alive and
 * sized, the eglSwapBuffers wrapper (FPS counter and cursor overlay, swap timing), and timed
 * wrappers for the GL calls that can wait for the GPU (counted for the [perf] log).
 * Split out of imports.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); extended for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>
#include "nx_pointer.h"
#include "rd_fps.h"
#include "rd_platform.h"

#include "config.h"
#include "rd_menu.h"
#include "egl.h"
#include "android_native_unity.h"
#include "rd_perfstat.h"
#include "rd_gltrace_gl.h"

// ---------------------------------------------------------------------------
// EGL surface-size fixup
// ---------------------------------------------------------------------------

// switch-mesa returns 0 for EGL_WIDTH/EGL_HEIGHT on the window surface even
// though it renders full-screen; the engine trusts that and sets a 0x0 viewport
// (nothing draws). Hand back the real panel size instead.
EGLBoolean egl_QuerySurface_log(EGLDisplay d, EGLSurface s, EGLint attr, EGLint *val) {
  EGLBoolean r = eglQuerySurface(d, s, attr, val);
  if (val) {
    if (attr == 0x3057 /*EGL_WIDTH*/  && *val <= 0) { *val = screen_width;  r = EGL_TRUE; }
    if (attr == 0x3056 /*EGL_HEIGHT*/ && *val <= 0) { *val = screen_height; r = EGL_TRUE; }
  }
  return r;
}

// SINGLETON window surface. libnx's one NWindow hosts exactly one swapchain, but Unity
// creates the window surface then re-creates it on gfx-state recreation; a 2nd
// eglCreateWindowSurface on the same NWindow corrupts the buffer queue and the present
// faults the vi compositor. So create it once and hand the same EGLSurface back; no-op destroy.
static EGLSurface g_win_surface = NULL;
static void      *g_win_native  = NULL;
extern EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, void *, const EGLint *);
EGLSurface egl_CreateWindowSurface_log(EGLDisplay d, EGLConfig cfg, void *win, const EGLint *al) {
  if (g_win_surface && g_win_native == win) return g_win_surface;
  EGLSurface sfc = eglCreateWindowSurface(d, cfg, win, al);
  if (sfc && sfc != (EGLSurface)0) { g_win_surface = sfc; g_win_native = win; }
  nx_window_reassert();   /* mesa may reset the nwindow crop/transform when it builds its swapchain */
  return sfc;
}
extern EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface);
EGLBoolean egl_DestroySurface_keep(EGLDisplay d, EGLSurface s) {
  if (s && s == g_win_surface) return EGL_TRUE;   /* keep the one swapchain alive */
  return eglDestroySurface(d, s);
}


/* config.txt debug_fps_counter: frame rate of the presented frames, top-left. The text is
 * laid out again only when rd_fps publishes a new value (twice a second). */
static void fps_overlay_draw(void) {
  static rd_fps fps = RD_FPS_INIT;
  static rd_rect glyphs[RD_OVERLAY_MAX_RECTS], panel;
  static int nglyphs = -1;
  if (rd_fps_frame(&fps, rd_now_ns(), RD_FPS_WINDOW_NS) || nglyphs < 0) {
    char text[32];
    rd_fps_format(&fps, text, sizeof text);
    const float px = 3.0f * (float)screen_height / 720.0f;   /* 3 px at 720p, 4.5 at 1080p */
    const float pad = px, margin = 2.0f * px;
    panel = (rd_rect){ margin, margin, rd_fps_text_width(text, px) + 2.0f * pad,
                       RD_FPS_GLYPH_H * px + 2.0f * pad };
    nglyphs = rd_fps_layout(text, margin + pad, margin + pad, px, glyphs,
                            (int)(sizeof glyphs / sizeof glyphs[0]));
  }
  if (!rd_overlay_begin(screen_width, screen_height)) return;
  rd_overlay_rects(&panel, 1, 0.0f, 0.0f, 0.0f, 0.6f);
  rd_overlay_rects(glyphs, nglyphs, 1.0f, 1.0f, 0.2f, 1.0f);
  rd_overlay_end();
}

EGLBoolean egl_SwapBuffers_log(EGLDisplay d, EGLSurface s) {
  /* Re-assert the nwindow transform for the first few frames while mesa settles its swapchain. */
  static unsigned frame = 0;
  if (frame < 5) { frame++; nx_window_reassert(); }
  if (config.debug_gl_trace) {   /* before the overlays: the game's frame only */
    static unsigned traced;
    if (++traced % RD_GLTRACE_PERIOD_FRAMES == 0) rd_gltrace_arm("every 10 s");
    rd_gltrace_frame_end();
  }
  if (config.debug_fps_counter) fps_overlay_draw();
  nxp_draw();   /* on-screen cursor overlay; saves/restores all GL state */
  rd_menu_draw();   /* menu navigation focus frame */
  const uint64_t t0 = rd_now_ns();
  const EGLBoolean r = eglSwapBuffers(d, s);
  __atomic_add_fetch(&nx_stat_swap_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  return r;
}

/* GL calls that can block until the GPU is done with a buffer (uploads, maps, syncs). The
 * time spent in them goes to nx_stat_glwait_ns -> "gl=" in the [perf] line: large values on
 * slow frames mean the CPU waits for the GPU there (e.g. re-uploading the smoke's particle
 * mesh into a buffer the GPU still reads). Unity gets them through eglGetProcAddress. */
#define GL_TIMED(call) do {                                                      \
    const uint64_t _t0 = rd_now_ns();                                            \
    call;                                                                        \
    __atomic_add_fetch(&nx_stat_glwait_ns, rd_now_ns() - _t0, __ATOMIC_RELAXED); \
  } while (0)

static void (*real_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void *);
static void *(*real_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
static GLboolean (*real_glUnmapBuffer)(GLenum);
static GLenum (*real_glClientWaitSync)(GLsync, GLbitfield, GLuint64);

void timed_glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) {
  GL_TIMED(glBufferData(t, n, d, u));
}
static void timed_glBufferSubData(GLenum t, GLintptr o, GLsizeiptr n, const void *d) {
  GL_TIMED(real_glBufferSubData(t, o, n, d));
}
static void *timed_glMapBufferRange(GLenum t, GLintptr o, GLsizeiptr n, GLbitfield a) {
  void *r; GL_TIMED(r = real_glMapBufferRange(t, o, n, a)); return r;
}
static GLboolean timed_glUnmapBuffer(GLenum t) {
  GLboolean r; GL_TIMED(r = real_glUnmapBuffer(t)); return r;
}
static GLenum timed_glClientWaitSync(GLsync s, GLbitfield f, GLuint64 timeout) {
  GLenum r; GL_TIMED(r = real_glClientWaitSync(s, f, timeout)); return r;
}
static void timed_glFinish(void) { GL_TIMED(glFinish()); }

void *egl_wrap_gl(const char *name, void *real) {
  void *w = config.debug_gl_trace && real ? rd_gltrace_wrap(name, real) : NULL;
  return w ? w : real;
}

void *egl_GetProcAddress_timed(const char *name) {
  void *real = (void *)eglGetProcAddress(name);
  if (!real || !name) return real;
  void *traced = egl_wrap_gl(name, real);   /* the frame trace (rd_gltrace_gl.h) */
  if (traced != real) return traced;
#define TIMED(fn) if (!strcmp(name, #fn)) { real_##fn = real; return (void *)&timed_##fn; }
  TIMED(glBufferSubData) TIMED(glMapBufferRange) TIMED(glUnmapBuffer) TIMED(glClientWaitSync)
#undef TIMED
  if (!strcmp(name, "glBufferData")) return (void *)&timed_glBufferData;
  if (!strcmp(name, "glFinish"))     return (void *)&timed_glFinish;
  return real;
}
