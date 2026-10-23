/* rd_etc2bc_gl.c -- see rd_etc2bc_gl.h. MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include <GLES3/gl31.h>
#include "rd_etc2bc.h"
#include "rd_etc2bc_gl.h"
#include "rd_jobs.h"
#include "rd_platform.h"
#include "util.h"

#define PARALLEL_BLOCKS 4096      /* smaller images are re-encoded by the caller alone */
#define PARTS_PER_THREAD 4        /* row ranges per thread: a slow core takes fewer */

static void (*real_tex_storage)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static void (*real_cimage)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
static void (*real_csub)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei,
                         const void *);

static uint32_t g_uploads;
static uint64_t g_blocks, g_ns;

void rd_etc2bc_stats(uint32_t *uploads, uint64_t *blocks, uint64_t *ns) {
  *uploads = __atomic_load_n(&g_uploads, __ATOMIC_RELAXED);
  *blocks = __atomic_load_n(&g_blocks, __ATOMIC_RELAXED);
  *ns = __atomic_load_n(&g_ns, __ATOMIC_RELAXED);
}

/* One image being re-encoded, shared by the workers. */
typedef struct {
  uint32_t fmt;
  const uint8_t *src;
  uint8_t *dst;
  int blocks_w, blocks_h;
} image;

static void encode_part(void *ctx, int part, int parts) {
  const image *im = ctx;
  int lo, hi;
  rd_etc2bc_split(im->blocks_h, parts, part, &lo, &hi);
  rd_etc2bc_rows(im->fmt, im->src, im->dst, im->blocks_w, lo, hi);
}

/* The output buffer, kept and grown to the largest image (no malloc per upload). */
static uint8_t *g_buf;
static size_t g_buf_size;

/* Re-encodes a w x h ETC image of `size` bytes into the output buffer; NULL when the size does
 * not match the dimensions (left to mesa as it is) or there is no memory. */
static const uint8_t *encode(uint32_t fmt, int w, int h, GLsizei size, const void *src) {
  image im = { .fmt = fmt, .src = src };
  rd_etc2bc_blocks(w, h, &im.blocks_w, &im.blocks_h);
  const size_t blocks = (size_t)im.blocks_w * (size_t)im.blocks_h;
  const size_t bytes = blocks * (size_t)rd_etc2bc_block_bytes(fmt);
  if (!src || size < 0 || (size_t)size != bytes || bytes == 0) return NULL;
  if (bytes > g_buf_size) {
    uint8_t *b = realloc(g_buf, bytes);
    if (!b) return NULL;
    g_buf = b;
    g_buf_size = bytes;
  }
  im.dst = g_buf;
  const uint64_t t0 = rd_now_ns();
  if (blocks < PARALLEL_BLOCKS) {
    rd_etc2bc_rows(fmt, im.src, im.dst, im.blocks_w, 0, im.blocks_h);
  } else {
    rd_jobs_run(encode_part, &im, (RD_JOBS_MAX_WORKERS + 1) * PARTS_PER_THREAD);
  }
  __atomic_add_fetch(&g_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  __atomic_add_fetch(&g_blocks, blocks, __ATOMIC_RELAXED);
  __atomic_add_fetch(&g_uploads, 1, __ATOMIC_RELAXED);
  return im.dst;
}

/* The client pointer of upload data: `data` itself, or mapped from the bound pixel unpack
 * buffer (where `data` is an offset); *pbo = that buffer (0 = none), unmapped and unbound by
 * release() after the upload. */
static const void *acquire(const void *data, GLsizei size, GLint *pbo) {
  *pbo = 0;
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, pbo);
  if (!*pbo) return data;
  return glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, (GLintptr)data, size, GL_MAP_READ_BIT);
}

/* After acquire: unmaps the buffer and unbinds it, so the re-encoded data is read from client
 * memory; restore() binds it again. */
static void release(GLint pbo) {
  if (!pbo) return;
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static void restore(GLint pbo) {
  if (pbo) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)pbo);
}

static void w_tex_storage(GLenum t, GLsizei levels, GLenum f, GLsizei w, GLsizei h) {
  const uint32_t bc = rd_etc2bc_target(f);
  real_tex_storage(t, levels, bc ? bc : f, w, h);
}

static void w_cimage(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei n,
                     const void *p) {
  const uint32_t bc = rd_etc2bc_target(f);
  if (!bc) { real_cimage(t, l, f, w, h, b, n, p); return; }
  GLint pbo;
  const void *src = acquire(p, n, &pbo);
  const uint8_t *out = encode(f, w, h, n, src);
  release(pbo);
  if (out) real_cimage(t, l, bc, w, h, b, n, out);
  restore(pbo);
  if (!out) real_cimage(t, l, f, w, h, b, n, p);   /* as the engine asked: mesa decompresses */
}

/* 1 if level l of the texture bound at target t (a 2D target or a cube face) is stored as
 * S3TC `bc`, i.e. was allocated by w_tex_storage / w_cimage. */
static int stored_as(GLenum t, GLint l, uint32_t bc) {
  GLint fmt = 0;
  glGetTexLevelParameteriv(t, l, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
  return (uint32_t)fmt == bc;
}

static void w_csub(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLsizei n,
                   const void *p) {
  const uint32_t bc = rd_etc2bc_target(f);
  if (!bc || !stored_as(t, l, bc)) { real_csub(t, l, x, y, w, h, f, n, p); return; }
  GLint pbo;
  const void *src = acquire(p, n, &pbo);
  const uint8_t *out = encode(f, w, h, n, src);
  release(pbo);
  if (out) real_csub(t, l, x, y, w, h, bc, n, out);
  restore(pbo);
  if (!out) real_csub(t, l, x, y, w, h, f, n, p);  /* fails: GL reports the format mismatch */
}

void *rd_etc2bc_wrap(const char *name, void *real) {
#define WRAP(gl, var, w) if (!strcmp(name, gl)) { var = real; return (void *)&w; }
  WRAP("glTexStorage2D", real_tex_storage, w_tex_storage)
  WRAP("glCompressedTexImage2D", real_cimage, w_cimage)
  WRAP("glCompressedTexSubImage2D", real_csub, w_csub)
#undef WRAP
  return NULL;
}
