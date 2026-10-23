/* sources: source/platform/rd_loadprof.c */
/* Host unit test of rd_loadprof (debug_load lines). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_platform.h"
#include "rd_loadprof.h"

#define MS_ RD_NS_PER_MS

/* A frame's traffic is the difference of the counters; the line shows engine calls and bytes
 * against SD opens / reads with the average SD read size, the cache hits / misses, the writes
 * and SD commits, and the time since start. */
static void test_io(void) {
  const rd_loadprof_io a = { 1, 2, 3, 4, 1024, MS_, 1, MS_, 10, 10 * 1024, MS_, 7, 3,
                             5, 1024, MS_, 2, MS_ };
  const rd_loadprof_io b = { 11, 102, 53, 4, 1024 + 512 * 1024, 101 * MS_, 6, 21 * MS_,
                             110, 10 * 1024 + 100 * 131072, 81 * MS_, 907, 103,
                             26, 1024 + 3072 * 1024, 41 * MS_, 23, 1201 * MS_ };
  rd_loadprof_io d;
  rd_loadprof_io_diff(&a, &b, &d);
  CHECK(d.fopens == 10 && d.freads == 100 && d.fseeks == 50 && d.reads == 0);
  CHECK(d.sd_reads == 100 && d.sd_read_bytes == 100 * 131072);
  char buf[512];
  rd_loadprof_format_io(&d, 4000 * MS_, 9500 * MS_, buf, sizeof buf);
  CHECK(strstr(buf, "frame 4000 ms, at 9500 ms |") != NULL);
  CHECK(strstr(buf, "fopen 10, fread 100, fseek 50, read 0, 512 KB in 100 ms") != NULL);
  CHECK(strstr(buf, "open 5 in 20 ms, read 100 x avg 131072 B = 12800 KB in 80 ms") != NULL);
  CHECK(strstr(buf, "| cache: hit 900, miss 100") != NULL);
  CHECK(strstr(buf, "| write 21, 3072 KB in 40 ms | commit 21 in 1200 ms") != NULL);
  const rd_loadprof_io z = { 0 };
  rd_loadprof_format_io(&z, 0, 0, buf, sizeof buf);
  CHECK(strstr(buf, "avg 0 B") != NULL);                 /* no reads: no division */
}

/* Threads are matched by tid, listed busiest first with their share of the frame; idle ones
 * (under 1%), ones that appeared during the frame and the rest beyond the top are left out. */
static void test_cpu(void) {
  rd_loadprof_thread before[RD_LOADPROF_TOP + 3], after[RD_LOADPROF_TOP + 3];
  int n = 0;
  for (int i = 0; i < RD_LOADPROF_TOP + 2; i++, n++) {
    before[i] = (rd_loadprof_thread){ .tid = 100 + i, .cpu_ns = 5 * MS_ };
    after[i] = before[i];
    snprintf(after[i].name, sizeof after[i].name, "T%d", i);
    after[i].cpu_ns += (uint64_t)(i + 1) * 100 * MS_;     /* T0 100 ms .. T9 1000 ms */
  }
  after[0].cpu_ns = before[0].cpu_ns + 5 * MS_;          /* T0: 0.5% -> left out */
  after[n] = (rd_loadprof_thread){ .tid = 999, .name = "New", .cpu_ns = 900 * MS_ };
  char buf[512];
  rd_loadprof_format_cpu(before, n, after, n + 1, 1000 * MS_, buf, sizeof buf);
  CHECK(strncmp(buf, "cpu: T9 1000 ms (100%), T8 900 ms (90%)", 39) == 0);
  CHECK(strstr(buf, "T0 ") == NULL && strstr(buf, "New") == NULL);
  CHECK(strstr(buf, "T2 300 ms (30%)") != NULL && strstr(buf, "T1 ") == NULL);   /* top 8 */
  rd_loadprof_format_cpu(before, 1, after, 1, 1000 * MS_, buf, sizeof buf);
  CHECK(strcmp(buf, "cpu: no thread above 1%") == 0);
}

int main(void) {
  RUN(test_io);
  RUN(test_cpu);
  return g_failures != 0;
}
