/* rd_gltrace_gl.h -- the GL side of the frame trace (rd_gltrace.h, rd_gltrace_gl.c): wrappers
 * of the engine's render-target calls and the GPU wait per render target, only with config.txt
 * debug_gl_trace = 1 (a capture every RD_GLTRACE_PERIOD_FRAMES).
 * MIT (see LICENSE). */
#ifndef RD_GLTRACE_GL_H
#define RD_GLTRACE_GL_H

/* For egl_GetProcAddress_timed: the wrapper of GL function `name` (remembering `real`, mesa's),
 * or NULL when it is not traced. */
void *rd_gltrace_wrap(const char *name, void *real);

/* Asks for a capture of the next RD_GLTRACE_FRAMES frames, logged as `label` (a string that
 * stays valid). Any thread; a capture already running finishes first. */
void rd_gltrace_arm(const char *label);

/* GL thread, at the start of eglSwapBuffers: ends the recorded frame, collects finished
 * timestamps, logs a finished capture and starts recording the next frame. */
void rd_gltrace_frame_end(void);

#endif
