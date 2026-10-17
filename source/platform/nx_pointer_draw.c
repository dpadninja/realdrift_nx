/* nx_pointer_draw.c -- the on-screen cursor of nx_pointer: an optional cursor.png (read at
 * init, decoded and uploaded on the render thread) or the built-in arrow, drawn through
 * rd_overlay right before eglSwapBuffers.
 *
 * Split out of nx_pointer.c -- origin: badpiggies_nx / cloverpit_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, https://github.com/ChanseyIsTheBest/cloverpit_nx);
 * the stick cursor comes from laytonbmr_nx by naga (https://github.com/NaGaa95/laytonbmr_nx).
 * Copyright (c) 2026 cloverpit_nx contributors. MIT (see LICENSE). */
#include <GLES2/gl2.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>  /* png_jmpbuf / setjmp in the libpng error path */
#include <png.h>

#include "nx_pointer.h"
#include "nx_pointer_internal.h"
#include "rd_overlay.h"

/* --------------------------------------------------- custom cursor (PNG) */

#define CURSOR_MAX_DIM 64

/* Raw file bytes, slurped at init (single-threaded) so the render thread never
 * has to touch the filesystem. Decoded + uploaded lazily in nxp_draw(). */
static uint8_t *s_png_bytes = NULL;
static size_t   s_png_len   = 0;

/* decoded */
static GLuint s_cursor_tex = 0;
static int    s_cursor_w = 0, s_cursor_h = 0;
static int    s_png_tried = 0;      /* decode attempted (success or not) */

void nxp_slurp_cursor_png(void) {
  if (!nxp_cfg.data_dir) return;
  char path[512];
  snprintf(path, sizeof path, "%s/cursor.png", nxp_cfg.data_dir);

  FILE *f = nxp_fopen(path, "rb");     /* locked path: engine threads are live */
  if (!f) { nxp_logf("nxp: no cursor.png (using built-in arrow)\n"); return; }

  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len <= 0 || len > 4 * 1024 * 1024) { nxp_fclose(f); return; }

  s_png_bytes = malloc((size_t)len);
  if (!s_png_bytes) { nxp_fclose(f); return; }
  s_png_len = fread(s_png_bytes, 1, (size_t)len, f);
  nxp_fclose(f);

  if (s_png_len != (size_t)len) { free(s_png_bytes); s_png_bytes = NULL; s_png_len = 0; return; }
  nxp_logf("nxp: cursor.png loaded (%zu bytes), decoding on first frame\n", s_png_len);
}

/* libpng reader over the in-memory buffer */
typedef struct { const uint8_t *p; size_t len, off; } PngSrc;

static void png_read_mem(png_structp png, png_bytep out, png_size_t n) {
  PngSrc *s = (PngSrc *)png_get_io_ptr(png);
  if (s->off + n > s->len) { png_error(png, "short read"); return; }
  memcpy(out, s->p + s->off, n);
  s->off += n;
}

/* Decode to RGBA8 and upload. Returns 1 on success. Render thread only. */
static int cursor_upload_png(void) {
  if (!s_png_bytes) return 0;

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) return 0;
  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); return 0; }

  uint8_t   *pixels = NULL;
  png_bytep *rows   = NULL;
  if (setjmp(png_jmpbuf(png))) {          /* libpng error path */
    free(pixels); free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    nxp_logf("nxp: cursor.png decode failed -- using built-in arrow\n");
    return 0;
  }

  PngSrc src = { s_png_bytes, s_png_len, 0 };
  png_set_read_fn(png, &src, png_read_mem);
  png_read_info(png, info);

  const png_uint_32 w = png_get_image_width(png, info);
  const png_uint_32 h = png_get_image_height(png, info);
  if (w == 0 || h == 0 || w > CURSOR_MAX_DIM || h > CURSOR_MAX_DIM) {
    nxp_logf("nxp: cursor.png is %ux%u -- max is %dx%d, using built-in arrow\n",
          (unsigned)w, (unsigned)h, CURSOR_MAX_DIM, CURSOR_MAX_DIM);
    png_destroy_read_struct(&png, &info, NULL);
    return 0;
  }

  /* normalise anything to 8-bit RGBA so transparency always works */
  const int ct = png_get_color_type(png, info);
  const int bd = png_get_bit_depth(png, info);
  if (bd == 16)                       png_set_strip_16(png);
  if (ct == PNG_COLOR_TYPE_PALETTE)   png_set_palette_to_rgb(png);
  if (ct == PNG_COLOR_TYPE_GRAY && bd < 8) png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  /* ensure an alpha channel exists even for opaque RGB */
  png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  png_read_update_info(png, info);

  const size_t stride = (size_t)w * 4;
  pixels = malloc(stride * h);
  rows   = malloc(sizeof(png_bytep) * h);
  if (!pixels || !rows) { png_error(png, "oom"); }
  for (png_uint_32 y = 0; y < h; y++) rows[y] = pixels + y * stride;
  png_read_image(png, rows);
  png_read_end(png, NULL);
  png_destroy_read_struct(&png, &info, NULL);
  free(rows);

  /* Save the texture binding we are about to disturb -- this runs inside the
   * engine's context, and leaving its texture unbound would corrupt its frame. */
  GLint prev_active = 0, prev_tex = 0;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);

  glGenTextures(1, &s_cursor_tex);
  glBindTexture(GL_TEXTURE_2D, s_cursor_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  GLint prev_align = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);   /* engine uploads assume 4 */
  free(pixels);

  glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);   /* put the engine's back */
  glActiveTexture((GLenum)prev_active);

  s_cursor_w = (int)w;
  s_cursor_h = (int)h;
  nxp_logf("nxp: custom cursor %dx%d ready (tex=%u)\n", s_cursor_w, s_cursor_h, s_cursor_tex);
  return 1;
}


/* ======================= GL overlay ====================================== */

/* Built-in arrow: tip at (0,0), y down, drawn as a fan from the tip. */
static const GLfloat s_arrow[] = {
   0.0f,  0.0f,   0.0f, 16.0f,   4.0f, 12.0f,   7.0f, 18.0f,
  10.0f, 16.5f,   7.0f, 10.5f,  12.0f, 10.0f,
};
#define ARROW_VERTS 7

void nxp_draw(void) {
  if (!nxp_ready || nxp_visible <= 0) return;

  /* decode + upload cursor.png on first draw (needs a live GL context) */
  if (!s_png_tried) {
    s_png_tried = 1;
    if (s_png_bytes) {
      if (!cursor_upload_png()) { s_cursor_tex = 0; }
      free(s_png_bytes); s_png_bytes = NULL; s_png_len = 0;   /* done with the bytes */
    }
  }

  if (!rd_overlay_begin(nxp_cfg.screen_w, nxp_cfg.screen_h)) return;

  if (s_cursor_tex) {
    /* ---- custom PNG: textured quad, hotspot at the top-left ---- */
    const GLfloat w = (GLfloat)s_cursor_w, h = (GLfloat)s_cursor_h;
    const GLfloat quad[] = { 0,0,  w,0,  0,h,  w,h };
    const GLfloat uv[]   = { 0,0,  1,0,  0,1,  1,1 };
    rd_overlay_textured(RD_OVERLAY_STRIP, s_cursor_tex, quad, uv, 4,
                        nxp_cx, nxp_cy, (GLfloat)nxp_cfg.screen_h / 1080.0f);
  } else {
    /* ---- built-in arrow: solid colour, no texture touched at all ---- */
    const GLfloat sc = 2.4f * ((GLfloat)nxp_cfg.screen_h / 1080.0f);
    rd_overlay_solid(RD_OVERLAY_FAN, s_arrow, ARROW_VERTS, nxp_cx, nxp_cy, sc * 1.22f,
                     0.0f, 0.0f, 0.0f, 0.85f);                              /* outline */
    rd_overlay_solid(RD_OVERLAY_FAN, s_arrow, ARROW_VERTS, nxp_cx, nxp_cy, sc,
                     1.0f, 1.0f, 1.0f, 1.0f);                               /* fill */
  }

  rd_overlay_end();
}
