/* rd_overlay_gl.c -- the GL side of rd_overlay.h: one small shader program and a full
 * save/restore of the state it touches. Moved out of nx_pointer.c (the cursor overlay)
 * so the FPS counter can share it.
 *
 * Origin of the moved code: badpiggies_nx by ChanseyIsTheBest,
 * https://github.com/ChanseyIsTheBest/badpiggies_nx
 * (commit 7726be3); the same module ships in cloverpit_nx by ChanseyIsTheBest,
 * https://github.com/ChanseyIsTheBest/cloverpit_nx. The stick cursor and its GL overlay were
 * ported there from laytonbmr_nx by naga, https://github.com/NaGaa95/laytonbmr_nx.
 *
 * Copyright (c) 2026 cloverpit_nx contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
#include <stddef.h>
#include <GLES3/gl3.h>
#include "rd_overlay.h"
#include "util.h"   /* debugPrintf */

static GLuint s_prog = 0;
static GLint  s_u_screen, s_u_origin, s_u_scale, s_u_colour, s_u_tex, s_u_use_tex;
static int    s_gl_failed = 0;

/* Compiles one shader; 0 on a compile error. */
static GLuint mkshader(GLenum t, const char *src) {
  GLuint s = glCreateShader(t);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { glDeleteShader(s); return 0; }
  return s;
}

/* Builds the overlay program and looks up its uniforms on first use; 1 if ready, 0 forever
 * after a failure (logged). */
static int gl_init(void) {
  if (s_prog) return 1;
  if (s_gl_failed) return 0;

  static const char *vs =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "uniform vec2 uScreen;\n"
    "uniform vec2 uOrigin;\n"
    "uniform float uScale;\n"
    "void main() {\n"
    "  vUV = aUV;\n"
    "  vec2 p = uOrigin + aPos * uScale;\n"
    "  gl_Position = vec4((p.x/uScreen.x)*2.0-1.0, 1.0-(p.y/uScreen.y)*2.0, 0.0, 1.0);\n"
    "}\n";
  static const char *fs =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform vec4 uColour;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uUseTex;\n"
    "void main() {\n"
    /* Branch on a uniform (uniform control flow -- always safe in GLES2) so the solid
     * path never samples a texture. That means we don't have to bind or disturb ANY
     * texture state unless a textured draw (custom cursor.png) is made. */
    "  if (uUseTex > 0.5) gl_FragColor = texture2D(uTex, vUV);\n"
    "  else               gl_FragColor = uColour;\n"
    "}\n";

  GLuint v = mkshader(GL_VERTEX_SHADER, vs), f = mkshader(GL_FRAGMENT_SHADER, fs);
  if (!v || !f) { s_gl_failed = 1; debugPrintf("[overlay] shader compile failed\n"); return 0; }

  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glBindAttribLocation(p, 0, "aPos");
  glBindAttribLocation(p, 1, "aUV");
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);

  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { glDeleteProgram(p); s_gl_failed = 1; debugPrintf("[overlay] link failed\n"); return 0; }

  s_prog      = p;
  s_u_screen  = glGetUniformLocation(p, "uScreen");
  s_u_origin  = glGetUniformLocation(p, "uOrigin");
  s_u_scale   = glGetUniformLocation(p, "uScale");
  s_u_colour  = glGetUniformLocation(p, "uColour");
  s_u_tex     = glGetUniformLocation(p, "uTex");
  s_u_use_tex = glGetUniformLocation(p, "uUseTex");
  return 1;
}

/* A vertex attribute's FULL state. Saving only the enabled flag (as we did at
 * first) is not enough: glVertexAttribPointer also records size/type/stride/
 * pointer AND which ARRAY_BUFFER was bound at the time. The engine sets its
 * attribute pointers once and reuses them across frames, so overwriting slot 0
 * with our arrow array left every subsequent engine draw reading garbage
 * geometry -- 25 draw calls a frame, and a black screen. */
typedef struct {
  GLint enabled, size, type, norm, stride, buf;
  void *ptr;
} AttribState;

/* Saves everything glVertexAttribPointer and the enable flag set for attribute i. */
static void attrib_save(GLuint i, AttribState *a) {
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &a->enabled);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &a->size);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &a->type);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &a->norm);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &a->stride);
  glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &a->buf);
  glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &a->ptr);
}

/* Restores attribute i, rebinding the buffer it was captured with first. */
static void attrib_restore(GLuint i, const AttribState *a) {
  /* glVertexAttribPointer captures whatever ARRAY_BUFFER is bound right now, so
   * re-bind the attribute's original buffer before restoring its pointer. */
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)a->buf);
  if (a->size > 0)
    glVertexAttribPointer(i, a->size, (GLenum)a->type,
                          (GLboolean)(a->norm ? GL_TRUE : GL_FALSE),
                          a->stride, a->ptr);
  if (a->enabled) glEnableVertexAttribArray(i);
  else            glDisableVertexAttribArray(i);
}

/* Everything an overlay draw changes, saved by rd_overlay_begin() (render thread only). */
static struct {
  AttribState a0, a1;
  GLint vao, prog, buf, bs_rgb, bd_rgb, bs_a, bd_a;
  GLboolean blend, depth, cull, scissor;
} s_saved;

static const GLenum PRIM[] = {
  [RD_OVERLAY_TRIANGLES] = GL_TRIANGLES,
  [RD_OVERLAY_STRIP]     = GL_TRIANGLE_STRIP,
  [RD_OVERLAY_FAN]       = GL_TRIANGLE_FAN,
};

int rd_overlay_begin(int screen_w, int screen_h) {
  if (screen_w <= 0 || screen_h <= 0 || !gl_init()) return 0;

  /* Client-side arrays are allowed only with vertex array object 0 (GLES 3.0): with the
   * engine's VAO still bound every glVertexAttribPointer below failed (GL_INVALID_OPERATION
   * "non-VBO array", thousands a minute, analysis/research/35) and the draw read the engine's
   * attributes. The attribute state saved and restored below is VAO 0's own. */
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s_saved.vao);
  glBindVertexArray(0);
  attrib_save(0, &s_saved.a0);
  attrib_save(1, &s_saved.a1);
  glGetIntegerv(GL_CURRENT_PROGRAM, &s_saved.prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s_saved.buf);
  glGetIntegerv(GL_BLEND_SRC_RGB, &s_saved.bs_rgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &s_saved.bd_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &s_saved.bs_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &s_saved.bd_a);
  s_saved.blend   = glIsEnabled(GL_BLEND);
  s_saved.depth   = glIsEnabled(GL_DEPTH_TEST);
  s_saved.cull    = glIsEnabled(GL_CULL_FACE);
  s_saved.scissor = glIsEnabled(GL_SCISSOR_TEST);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   /* honours PNG alpha */

  glUseProgram(s_prog);
  glUniform2f(s_u_screen, (GLfloat)screen_w, (GLfloat)screen_h);
  glBindBuffer(GL_ARRAY_BUFFER, 0);                    /* client-side arrays */
  return 1;
}

/* Attributes first (each re-binds its own source buffer), then the engine's VAO and the
 * global ARRAY_BUFFER binding, then the program and the fixed-function bits. */
void rd_overlay_end(void) {
  attrib_restore(0, &s_saved.a0);
  attrib_restore(1, &s_saved.a1);
  glBindVertexArray((GLuint)s_saved.vao);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)s_saved.buf);
  glUseProgram((GLuint)s_saved.prog);
  glBlendFuncSeparate((GLenum)s_saved.bs_rgb, (GLenum)s_saved.bd_rgb,
                      (GLenum)s_saved.bs_a, (GLenum)s_saved.bd_a);
  if (!s_saved.blend) glDisable(GL_BLEND); else glEnable(GL_BLEND);
  if (s_saved.depth)   glEnable(GL_DEPTH_TEST);
  if (s_saved.cull)    glEnable(GL_CULL_FACE);
  if (s_saved.scissor) glEnable(GL_SCISSOR_TEST);
}

void rd_overlay_solid(int prim, const float *xy, int verts, float ox, float oy, float scale,
                      float r, float g, float b, float a) {
  glEnableVertexAttribArray(0);
  glDisableVertexAttribArray(1);                       /* may still point at a textured draw's uv */
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
  glUniform2f(s_u_origin, ox, oy);
  glUniform1f(s_u_scale, scale);
  glUniform1f(s_u_use_tex, 0.0f);                      /* shader will not sample */
  glUniform4f(s_u_colour, r, g, b, a);
  glDrawArrays(PRIM[prim], 0, verts);
}

void rd_overlay_textured(int prim, unsigned tex, const float *xy, const float *uv, int verts,
                         float ox, float oy, float scale) {
  /* Texture units: read the ACTIVE unit first, then switch to unit 0 and read
   * ITS binding. (Reading GL_TEXTURE_BINDING_2D before switching gives you the
   * active unit's binding, and restoring that onto unit 0 corrupts unit 0.) */
  GLint prev_active = 0, tex0 = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, xy);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);

  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
  glUniform1i(s_u_tex, 0);
  glUniform1f(s_u_use_tex, 1.0f);
  glUniform2f(s_u_origin, ox, oy);
  glUniform1f(s_u_scale, scale);
  glUniform4f(s_u_colour, 1, 1, 1, 1);
  glDrawArrays(PRIM[prim], 0, verts);

  glBindTexture(GL_TEXTURE_2D, (GLuint)tex0);          /* restore unit 0 */
  glActiveTexture((GLenum)prev_active);                /* then the active unit */
}

void rd_overlay_rects(const rd_rect *rects, int n, float r, float g, float b, float a) {
  static float v[RD_OVERLAY_MAX_RECTS * RD_OVERLAY_RECT_FLOATS];   /* render thread only */
  n = rd_overlay_rect_tris(rects, n, v, RD_OVERLAY_MAX_RECTS);
  if (n > 0) rd_overlay_solid(RD_OVERLAY_TRIANGLES, v, n * 6, 0.0f, 0.0f, 1.0f, r, g, b, a);
}
