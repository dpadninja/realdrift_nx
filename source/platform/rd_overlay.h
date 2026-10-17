/* rd_overlay.h -- 2D drawing on top of the finished frame: the on-screen cursor
 * (nx_pointer.c) and the debug FPS counter (imports.c).
 *
 * Coordinates are render pixels, origin top-left, y down. A draw is a
 * rd_overlay_begin() / rd_overlay_end() pair on the render thread with the engine's GL
 * context current -- the natural place is the eglSwapBuffers wrapper, right before the
 * real swap. begin saves every bit of GL state an overlay draw touches and end puts it
 * back, so the engine never sees our draws.
 *
 * rd_overlay.c is the GL-free part (host-tested, tests/c/test_rd_overlay.c);
 * rd_overlay_gl.c holds the shader and the GL calls.
 * MIT (see LICENSE). */
#ifndef RD_OVERLAY_H
#define RD_OVERLAY_H

typedef struct { float x, y, w, h; } rd_rect;

/* Primitive of a vertex list (GL_TRIANGLES / GL_TRIANGLE_STRIP / GL_TRIANGLE_FAN). */
enum { RD_OVERLAY_TRIANGLES, RD_OVERLAY_STRIP, RD_OVERLAY_FAN };

#define RD_OVERLAY_MAX_RECTS 128   /* per rd_overlay_rects() call */
#define RD_OVERLAY_RECT_FLOATS 12  /* two triangles, x y per vertex */

/* Writes two triangles per rectangle (RD_OVERLAY_RECT_FLOATS floats each) for at most
 * `max` rectangles. Returns how many were converted. */
int rd_overlay_rect_tris(const rd_rect *r, int n, float *out, int max);

/* Saves the GL state and selects the overlay program for a screen_w x screen_h target.
 * Returns 0 (nothing saved, do not draw or call end) if the program cannot be built. */
int  rd_overlay_begin(int screen_w, int screen_h);
void rd_overlay_end(void);

/* A solid vertex list, each vertex placed at origin + xy * scale. */
void rd_overlay_solid(int prim, const float *xy, int verts, float ox, float oy, float scale,
                      float r, float g, float b, float a);

/* A vertex list sampling texture `tex` (uv per vertex). Texture unit 0 and the active
 * unit are restored afterwards. */
void rd_overlay_textured(int prim, unsigned tex, const float *xy, const float *uv, int verts,
                         float ox, float oy, float scale);

/* Solid rectangles in one colour (at most RD_OVERLAY_MAX_RECTS). */
void rd_overlay_rects(const rd_rect *rects, int n, float r, float g, float b, float a);

/* A draw the mod layer adds on top of the frame (its own begin/end), called by the
 * eglSwapBuffers wrapper; NULL = none. Set once at boot. */
typedef void (*rd_overlay_draw_fn)(int screen_w, int screen_h);
/* Sets the mod draw (rd_mods_boot) and returns it (the eglSwapBuffers wrapper). */
void rd_overlay_set_mod_draw(rd_overlay_draw_fn fn);
rd_overlay_draw_fn rd_overlay_mod_draw(void);

#endif
