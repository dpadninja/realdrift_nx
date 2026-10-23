/* rd_memstat.c -- see rd_memstat.h. MIT (see LICENSE). */
#include <stdio.h>
#include "rd_platform.h"
#include "rd_memstat.h"

#define MB(x) ((unsigned long long)((x) >> 20))

uint64_t rd_memstat_heap_avail(const rd_memstat *s) {
  const uint64_t untouched = s->heap_size > s->heap_top ? s->heap_size - s->heap_top : 0;
  const uint64_t freed = s->heap_top > s->heap_inuse ? s->heap_top - s->heap_inuse : 0;
  return untouched + freed;
}

uint64_t rd_memstat_heap_contig(const rd_memstat *s) {
  const uint64_t untouched = s->heap_size > s->heap_top ? s->heap_size - s->heap_top : 0;
  return untouched + s->heap_top_free;
}

int rd_memstat_format(const rd_memstat *s, const rd_memstat *prev, uint64_t low, char *buf,
                      size_t n) {
  /* the change in whole MB, signed; 0 without a previous snapshot */
  const long long delta = prev ? ((long long)s->heap_inuse - (long long)prev->heap_inuse) / (1 << 20)
                               : 0;
  return snprintf(buf, n,
                  "heap %llu/%llu MB (%+lld), avail %llu MB (top %llu, low %llu) | arena %llu/%llu MB | "
                  "oc %llu/%llu MB | fallback %llu MB | sys %llu/%llu MB",
                  MB(s->heap_inuse), MB(s->heap_size), delta, MB(rd_memstat_heap_avail(s)),
                  MB(rd_memstat_heap_contig(s)), MB(low), MB(s->arena_used), MB(s->arena_size), MB(s->oc_committed),
                  MB(s->oc_pool), MB(s->fallback), MB(s->sys_used), MB(s->sys_total));
}

const char *rd_memstat_due(uint32_t frame, uint64_t frame_ns) {
  if (frame_ns >= RD_MEM_LOAD_NS) return "after a load";
  if (frame > 0 && frame % RD_MEM_LOG_FRAMES == 0) return "every 10 s";
  return NULL;
}

#define GL_OUT_OF_MEMORY_ 0x0505u

int rd_memstat_gl_logged(uint32_t n, uint32_t err) {
  if (err == GL_OUT_OF_MEMORY_) return 1;
  return n >= 1 && (n <= RD_MEM_GL_LOG_FIRST || n % RD_MEM_GL_LOG_EVERY == 0);
}

const char *rd_memstat_gl_error_name(uint32_t err) {
  switch (err) {
    case 0x0500: return "GL_INVALID_ENUM";
    case 0x0501: return "GL_INVALID_VALUE";
    case 0x0502: return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY_: return "GL_OUT_OF_MEMORY";
    case 0x0506: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    default: {
      static char other[16];
      snprintf(other, sizeof other, "0x%04x", (unsigned)err);
      return other;
    }
  }
}
