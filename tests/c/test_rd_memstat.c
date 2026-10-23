/* sources: source/platform/rd_memstat.c */
/* Host unit test of rd_memstat (the debug_mem report). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_platform.h"
#include "rd_memstat.h"

#define MiB (1ull << 20)

/* What malloc can still give is the untouched rest of the heap plus the freed blocks inside
 * the part it has taken; inconsistent counts never wrap around. */
static void test_heap_avail(void) {
  rd_memstat s = { .heap_size = 2000 * MiB, .heap_top = 1500 * MiB, .heap_inuse = 1200 * MiB };
  CHECK(rd_memstat_heap_avail(&s) == 800 * MiB);
  s.heap_top = 2100 * MiB; s.heap_inuse = 2200 * MiB;
  CHECK(rd_memstat_heap_avail(&s) == 0);
}

/* The surely contiguous part is the untouched rest plus the free top block: 300 MB free in
 * pieces below the top do not count. */
static void test_heap_contig(void) {
  rd_memstat s = { .heap_size = 2000 * MiB, .heap_top = 1900 * MiB, .heap_inuse = 1500 * MiB,
                   .heap_top_free = 10 * MiB };
  CHECK(rd_memstat_heap_contig(&s) == 110 * MiB && rd_memstat_heap_avail(&s) == 500 * MiB);
  s.heap_top = 2100 * MiB;
  CHECK(rd_memstat_heap_contig(&s) == 10 * MiB);
}

/* The line shows in-use / total, the signed change since the previous snapshot, the low mark
 * and every other pool, in MB. */
static void test_format(void) {
  const rd_memstat prev = { .heap_size = 2032 * MiB, .heap_top = 1000 * MiB, .heap_inuse = 900 * MiB };
  rd_memstat s = prev;
  s.heap_inuse = 888 * MiB;
  s.arena_used = 512 * MiB; s.arena_size = 832 * MiB;
  s.oc_committed = 100 * MiB; s.oc_pool = 384 * MiB;
  s.sys_used = 3185 * MiB; s.sys_total = 3189 * MiB;
  char buf[256];
  rd_memstat_format(&s, &prev, 700 * MiB, buf, sizeof buf);
  CHECK(strstr(buf, "heap 888/2032 MB (-12)") != NULL);
  CHECK(strstr(buf, "avail 1144 MB (top 1032, low 700)") != NULL);
  CHECK(strstr(buf, "arena 512/832 MB") && strstr(buf, "oc 100/384 MB"));
  CHECK(strstr(buf, "fallback 0 MB") && strstr(buf, "sys 3185/3189 MB"));
  rd_memstat_format(&s, NULL, 0, buf, sizeof buf);
  CHECK(strstr(buf, "(+0)") != NULL);
}

/* A snapshot every RD_MEM_LOG_FRAMES and after every long (loading) frame, none otherwise and
 * none on frame 0. */
static void test_due(void) {
  CHECK(rd_memstat_due(RD_MEM_LOG_FRAMES, 0) != NULL);
  CHECK(rd_memstat_due(2 * RD_MEM_LOG_FRAMES, RD_FRAME_NS) != NULL);
  CHECK(rd_memstat_due(RD_MEM_LOG_FRAMES + 1, RD_FRAME_NS) == NULL);
  CHECK(rd_memstat_due(0, 0) == NULL);
  CHECK(!strcmp(rd_memstat_due(7, RD_MEM_LOAD_NS), "after a load"));
}

/* GL errors: the first ones all, then a sample, so a long failure stays visible but small;
 * an out-of-memory is never skipped: it is what the report looks for. */
static void test_gl_logged(void) {
  const uint32_t op = 0x0502, oom = 0x0505;
  CHECK(!rd_memstat_gl_logged(0, op));
  CHECK(rd_memstat_gl_logged(1, op) && rd_memstat_gl_logged(RD_MEM_GL_LOG_FIRST, op));
  CHECK(!rd_memstat_gl_logged(RD_MEM_GL_LOG_FIRST + 1, op));
  CHECK(rd_memstat_gl_logged(RD_MEM_GL_LOG_EVERY, op) && !rd_memstat_gl_logged(RD_MEM_GL_LOG_EVERY + 1, op));
  CHECK(rd_memstat_gl_logged(RD_MEM_GL_LOG_FIRST + 1, oom));
}

/* Known GL errors by name, others in hex. */
static void test_gl_error_name(void) {
  CHECK(!strcmp(rd_memstat_gl_error_name(0x0505), "GL_OUT_OF_MEMORY"));
  CHECK(!strcmp(rd_memstat_gl_error_name(0x0502), "GL_INVALID_OPERATION"));
  CHECK(!strcmp(rd_memstat_gl_error_name(0x0500), "GL_INVALID_ENUM"));
  CHECK(!strcmp(rd_memstat_gl_error_name(0x0501), "GL_INVALID_VALUE"));
  CHECK(!strcmp(rd_memstat_gl_error_name(0x0506), "GL_INVALID_FRAMEBUFFER_OPERATION"));
  CHECK(!strcmp(rd_memstat_gl_error_name(0x1234), "0x1234"));
}

int main(void) {
  RUN(test_heap_avail);
  RUN(test_heap_contig);
  RUN(test_format);
  RUN(test_due);
  RUN(test_gl_logged);
  RUN(test_gl_error_name);
  return g_failures != 0;
}
