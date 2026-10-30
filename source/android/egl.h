/* egl.h -- EGL/GLES wrappers between the engine and switch-mesa (egl.c).
 *
 * The import table (imports.c, unity_imports.c) hands these to the engine in place of the mesa
 * functions. All run on the engine's render thread with its GL context current.
 * MIT (see LICENSE). */
#ifndef EGL_H
#define EGL_H

#include <EGL/egl.h>
#include <GLES2/gl2.h>

/* eglQuerySurface: mesa reports 0 for EGL_WIDTH/HEIGHT of the window surface; answer the
 * render size instead (the engine would set a 0x0 viewport). */
EGLBoolean egl_QuerySurface_log(EGLDisplay d, EGLSurface s, EGLint attr, EGLint *val);
/* eglChooseConfig: with config.txt antialiasing > 0 asks mesa for that many samples (a
 * multisampled window, resolved at swap) and falls back to the engine's own request if none
 * exists; logs what was asked and the sample counts returned. */
EGLBoolean egl_ChooseConfig_msaa(EGLDisplay d, const EGLint *al, EGLConfig *cfg, EGLint size,
                                 EGLint *num);
/* eglCreateWindowSurface: remembers the one window surface and re-asserts the window
 * transform after mesa builds its swapchain. */
EGLSurface egl_CreateWindowSurface_log(EGLDisplay d, EGLConfig cfg, void *win, const EGLint *al);
/* eglDestroySurface: never destroys the window surface (the process keeps its one swapchain
 * for its whole life); other surfaces are destroyed normally. */
EGLBoolean egl_DestroySurface_keep(EGLDisplay d, EGLSurface s);
/* eglSwapBuffers: ends a traced frame (debug_gl_trace), draws the overlays (FPS counter,
 * cursor) on the finished frame, then swaps; the swap time goes to nx_stat_swap_ns. */
EGLBoolean egl_SwapBuffers_log(EGLDisplay d, EGLSurface s);
/* glBufferData timed into nx_stat_glwait_ns (it may wait for the GPU to release the buffer). */
void timed_glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u);
/* A GL function the engine gets (from our import table or eglGetProcAddress): with
 * debug_gl_trace its frame-trace wrapper (rd_gltrace_gl.h), with debug_mem its allocation
 * check (rd_memwatch.h), with texture_transcode the ETC2 -> S3TC wrapper on top
 * (rd_etc2bc_gl.h), with anisotropic_filtering the sampling wrapper (rd_aniso_gl.h), else
 * `real` itself. */
void *egl_wrap_gl(const char *name, void *real);
/* eglGetProcAddress for the engine: GL calls that can wait for the GPU come back wrapped so
 * their time is counted (nx_stat_glwait_ns, rd_perfstat.h), and with debug_gl_trace the
 * render-target calls the frame trace follows (rd_gltrace_gl.h); everything else is mesa's. */
void *egl_GetProcAddress_timed(const char *name);

#endif
