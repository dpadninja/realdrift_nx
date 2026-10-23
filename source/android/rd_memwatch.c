/* rd_memwatch.c -- see rd_memwatch.h. The snapshot: newlib's mallinfo and heap bounds, the
 * mmap pools (mmap_get_usage) and svcGetInfo. MIT (see LICENSE). */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <GLES3/gl3.h>
#include "config.h"
#include "libc_shim.h"   /* mmap_get_usage */
#include "rd_etc2bc_gl.h"
#include "rd_memstat.h"
#include "rd_memwatch.h"
#include "util.h"

extern char *fake_heap_start, *fake_heap_end;   /* the newlib heap (mem_layout.c) */

static void snapshot(rd_memstat *s) {
  const struct mallinfo mi = mallinfo();
  mmap_usage mu;
  mmap_get_usage(&mu);
  u64 used = 0, total = 0;
  svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  *s = (rd_memstat){
    .heap_size = (uint64_t)(fake_heap_end - fake_heap_start),
    .heap_top = (uint64_t)mi.arena, .heap_inuse = (uint64_t)mi.uordblks,
    .heap_top_free = (uint64_t)mi.keepcost,
    .arena_used = mu.arena_used, .arena_size = mu.arena_size,
    .oc_committed = mu.oc_committed, .oc_pool = mu.oc_pool, .fallback = mu.fallback,
    .sys_used = used, .sys_total = total,
  };
}

static Mutex g_lock;              /* the previous snapshot and the low mark */
static rd_memstat g_prev;
static int g_have_prev;
static uint64_t g_low = UINT64_MAX;

void rd_memwatch_log(const char *why) {
  rd_memstat s;
  snapshot(&s);
  char line[256];
  mutexLock(&g_lock);
  const uint64_t avail = rd_memstat_heap_avail(&s);
  if (avail < g_low) g_low = avail;
  rd_memstat_format(&s, g_have_prev ? &g_prev : NULL, g_low, line, sizeof line);
  g_prev = s;
  g_have_prev = 1;
  mutexUnlock(&g_lock);
  debugPrintf("[mem] (%s) %s\n", why, line);
  if (config.texture_transcode) {
    uint32_t uploads;
    uint64_t blocks, ns;
    rd_etc2bc_stats(&uploads, &blocks, &ns);
    debugPrintf("[mem] etc2bc so far: %u uploads, %.1f Mpix, %llu ms\n", (unsigned)uploads,
                (double)blocks * 16.0 / 1e6, (unsigned long long)(ns / 1000000u));
  }
}

void rd_memwatch_frame(uint32_t frame, uint64_t frame_ns) {
  const char *why = rd_memstat_due(frame, frame_ns);
  if (why) rd_memwatch_log(why);
}

/* ---- GL calls that allocate ------------------------------------------------------------ */

static uint32_t g_errors;         /* GL errors seen, all calls */

/* After an allocating call: logs a pending GL error with the call's size and format, and the
 * memory the first time and on every logged GL_OUT_OF_MEMORY. */
static void check(const char *fn, GLenum target, GLenum fmt, int w, int h, int d, long bytes) {
  const GLenum err = glGetError();
  if (err == GL_NO_ERROR) return;
  const uint32_t n = __atomic_add_fetch(&g_errors, 1, __ATOMIC_RELAXED);
  if (!rd_memstat_gl_logged(n, err)) return;
  debugPrintf("[mem] GL error #%u %s after %s target=0x%x fmt=0x%x %dx%dx%d bytes=%ld\n",
              (unsigned)n, rd_memstat_gl_error_name(err), fn, (unsigned)target, (unsigned)fmt,
              w, h, d, bytes);
  if (err == GL_OUT_OF_MEMORY) rd_memwatch_log("GL out of memory");
}

static void (*real_tex_image_2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                 const void *);
static void (*real_tex_image_3d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum,
                                 GLenum, const void *);
static void (*real_ctex_image_2d)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei,
                                  const void *);
static void (*real_ctex_image_3d)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei,
                                  const void *);
static void (*real_tex_storage_2d)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
static void (*real_tex_storage_3d)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
static void (*real_buffer_data)(GLenum, GLsizeiptr, const void *, GLenum);
static void (*real_rb_storage)(GLenum, GLenum, GLsizei, GLsizei);
static void (*real_rb_storage_ms)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);

static void w_tex_image_2d(GLenum t, GLint l, GLint f, GLsizei w, GLsizei h, GLint b, GLenum pf,
                           GLenum ty, const void *p) {
  real_tex_image_2d(t, l, f, w, h, b, pf, ty, p);
  check("glTexImage2D", t, (GLenum)f, w, h, 1, -1);
}
static void w_tex_image_3d(GLenum t, GLint l, GLint f, GLsizei w, GLsizei h, GLsizei d, GLint b,
                           GLenum pf, GLenum ty, const void *p) {
  real_tex_image_3d(t, l, f, w, h, d, b, pf, ty, p);
  check("glTexImage3D", t, (GLenum)f, w, h, d, -1);
}
static void w_ctex_image_2d(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei n,
                            const void *p) {
  real_ctex_image_2d(t, l, f, w, h, b, n, p);
  check("glCompressedTexImage2D", t, f, w, h, 1, n);
}
static void w_ctex_image_3d(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLsizei d, GLint b,
                            GLsizei n, const void *p) {
  real_ctex_image_3d(t, l, f, w, h, d, b, n, p);
  check("glCompressedTexImage3D", t, f, w, h, d, n);
}
static void w_tex_storage_2d(GLenum t, GLsizei levels, GLenum f, GLsizei w, GLsizei h) {
  real_tex_storage_2d(t, levels, f, w, h);
  check("glTexStorage2D", t, f, w, h, 1, -1);
}
static void w_tex_storage_3d(GLenum t, GLsizei levels, GLenum f, GLsizei w, GLsizei h, GLsizei d) {
  real_tex_storage_3d(t, levels, f, w, h, d);
  check("glTexStorage3D", t, f, w, h, d, -1);
}
static void w_buffer_data(GLenum t, GLsizeiptr n, const void *p, GLenum u) {
  real_buffer_data(t, n, p, u);
  check("glBufferData", t, 0, 0, 0, 0, (long)n);
}
static void w_rb_storage(GLenum t, GLenum f, GLsizei w, GLsizei h) {
  real_rb_storage(t, f, w, h);
  check("glRenderbufferStorage", t, f, w, h, 1, -1);
}
static void w_rb_storage_ms(GLenum t, GLsizei s, GLenum f, GLsizei w, GLsizei h) {
  real_rb_storage_ms(t, s, f, w, h);
  check("glRenderbufferStorageMultisample", t, f, w, h, s, -1);
}

void *rd_memwatch_wrap(const char *name, void *real) {
#define WRAP(gl, var, w) if (!strcmp(name, gl)) { var = real; return (void *)&w; }
  WRAP("glTexImage2D", real_tex_image_2d, w_tex_image_2d)
  WRAP("glTexImage3D", real_tex_image_3d, w_tex_image_3d)
  WRAP("glCompressedTexImage2D", real_ctex_image_2d, w_ctex_image_2d)
  WRAP("glCompressedTexImage3D", real_ctex_image_3d, w_ctex_image_3d)
  WRAP("glTexStorage2D", real_tex_storage_2d, w_tex_storage_2d)
  WRAP("glTexStorage3D", real_tex_storage_3d, w_tex_storage_3d)
  WRAP("glBufferData", real_buffer_data, w_buffer_data)
  WRAP("glRenderbufferStorage", real_rb_storage, w_rb_storage)
  WRAP("glRenderbufferStorageMultisample", real_rb_storage_ms, w_rb_storage_ms)
#undef WRAP
  return NULL;
}

/* ---- what one ETC2 texture costs ----------------------------------------------------- */

/* Heap growth of one 1024x1024 texture of `fmt` (`bytes` of data), made the way Unity makes
 * them (glTexStorage2D + glCompressedTexSubImage2D) but straight on mesa, logged against its
 * data size: about the same means mesa keeps it compressed; 4-8x means it decompressed it to
 * RGBA8 (ETC2 does; S3TC, what texture_transcode stores, should not). */
static void probe(const char *label, GLenum fmt, GLsizei bytes, const void *zeros) {
  const GLsizei side = 1024;
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  const size_t before = mallinfo().uordblks;
  glTexStorage2D(GL_TEXTURE_2D, 1, fmt, side, side);
  glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, side, side, fmt, bytes, zeros);
  glFinish();
  const size_t after = mallinfo().uordblks;
  const GLenum err = glGetError();
  glDeleteTextures(1, &tex);
  glFinish();
  const size_t freed = mallinfo().uordblks;
  debugPrintf("[mem] probe %s %dx%d: data %.2f MB, heap +%.2f MB (%.1fx), after delete %+.2f MB%s%s\n",
              label, side, side, bytes / 1048576.0, ((double)after - (double)before) / 1048576.0,
              ((double)after - (double)before) / bytes, ((double)freed - (double)before) / 1048576.0,
              err ? " -- GL error " : "", err ? rd_memstat_gl_error_name(err) : "");
}

void rd_memwatch_probe_once(void) {
  static int done;
  if (done) return;
  done = 1;
  GLint tex = 0, unpack = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);          /* the engine's state, put back below */
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack);
  while (glGetError() != GL_NO_ERROR) {}                /* earlier errors are not the probe's */
  if (unpack) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  const GLsizei rgba = 1024 * 1024;                     /* ETC2 RGBA8: 1 byte per pixel */
  void *zeros = calloc(1, (size_t)rgba);
  if (zeros) {
    probe("ETC2 RGB8", GL_COMPRESSED_RGB8_ETC2, rgba / 2, zeros);
    probe("ETC2 RGBA8", GL_COMPRESSED_RGBA8_ETC2_EAC, rgba, zeros);
    probe("S3TC DXT1", 0x83F0 /* GL_COMPRESSED_RGB_S3TC_DXT1_EXT */, rgba / 2, zeros);
    free(zeros);
  }
  if (unpack) glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack);
  glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
}

/* ---- failures under mesa: newlib memalign, nvmap, GPU address space -------------------- */
/* Linked with -Wl,--wrap (Makefile): libdrm_nouveau takes a buffer's memory with memalign,
 * registers it with nvMapCreate and maps it for the GPU with nvAddressSpaceMap; whichever
 * fails first is what GL reports as GL_OUT_OF_MEMORY. Only failures are logged, and only with
 * debug_mem. */

void *__real_memalign(size_t align, size_t n);
void *__wrap_memalign(size_t align, size_t n);
Result __real_nvMapCreate(NvMap *m, void *cpu_addr, u32 size, u32 align, NvKind kind,
                          bool is_cpu_cacheable);
Result __wrap_nvMapCreate(NvMap *m, void *cpu_addr, u32 size, u32 align, NvKind kind,
                          bool is_cpu_cacheable);
Result __real_nvAddressSpaceMap(NvAddressSpace *a, u32 handle, bool cacheable, NvKind kind,
                                iova_t *iova_out);
Result __wrap_nvAddressSpaceMap(NvAddressSpace *a, u32 handle, bool cacheable, NvKind kind,
                                iova_t *iova_out);

static uint32_t g_fail[3];        /* failures of memalign, nvMapCreate, nvAddressSpaceMap */
static int g_in_log;              /* the log line itself may allocate: no recursion */

/* Logs failure number n of `what` (sampled like GL errors), with the memory. */
static void failed(int which, const char *what, size_t size, size_t align, unsigned rc) {
  const uint32_t n = __atomic_add_fetch(&g_fail[which], 1, __ATOMIC_RELAXED);
  if (!config.debug_mem || !rd_memstat_gl_logged(n, 0)) return;
  if (__atomic_exchange_n(&g_in_log, 1, __ATOMIC_ACQUIRE)) return;
  debugPrintf("[mem] %s FAILED #%u: %zu bytes, align %zu, rc 0x%x\n", what, (unsigned)n, size,
              align, rc);
  rd_memwatch_log(what);
  __atomic_store_n(&g_in_log, 0, __ATOMIC_RELEASE);
}

void *__wrap_memalign(size_t align, size_t n) {
  void *p = __real_memalign(align, n);
  if (!p) failed(0, "memalign", n, align, 0);
  return p;
}

Result __wrap_nvMapCreate(NvMap *m, void *cpu_addr, u32 size, u32 align, NvKind kind,
                          bool is_cpu_cacheable) {
  const Result rc = __real_nvMapCreate(m, cpu_addr, size, align, kind, is_cpu_cacheable);
  if (R_FAILED(rc)) failed(1, "nvMapCreate", size, align, rc);
  return rc;
}

Result __wrap_nvAddressSpaceMap(NvAddressSpace *a, u32 handle, bool cacheable, NvKind kind,
                                iova_t *iova_out) {
  const Result rc = __real_nvAddressSpaceMap(a, handle, cacheable, kind, iova_out);
  if (R_FAILED(rc)) failed(2, "nvAddressSpaceMap", 0, 0, rc);
  return rc;
}
