/* rd_loadprof.c -- see rd_loadprof.h. MIT (see LICENSE). */
#include <stdio.h>
#include "rd_platform.h"
#include "rd_loadprof.h"

void rd_loadprof_io_diff(const rd_loadprof_io *a, const rd_loadprof_io *b, rd_loadprof_io *d) {
  d->fopens = b->fopens - a->fopens;
  d->freads = b->freads - a->freads;
  d->fseeks = b->fseeks - a->fseeks;
  d->reads = b->reads - a->reads;
  d->read_bytes = b->read_bytes - a->read_bytes;
  d->read_ns = b->read_ns - a->read_ns;
  d->sd_opens = b->sd_opens - a->sd_opens;
  d->sd_open_ns = b->sd_open_ns - a->sd_open_ns;
  d->sd_reads = b->sd_reads - a->sd_reads;
  d->sd_read_bytes = b->sd_read_bytes - a->sd_read_bytes;
  d->sd_read_ns = b->sd_read_ns - a->sd_read_ns;
  d->cache_hits = b->cache_hits - a->cache_hits;
  d->cache_misses = b->cache_misses - a->cache_misses;
  d->writes = b->writes - a->writes;
  d->write_bytes = b->write_bytes - a->write_bytes;
  d->write_ns = b->write_ns - a->write_ns;
  d->commits = b->commits - a->commits;
  d->commit_ns = b->commit_ns - a->commit_ns;
}

#define MS(ns) ((unsigned long long)((ns) / RD_NS_PER_MS))
#define KB(b)  ((unsigned long long)((b) >> 10))

int rd_loadprof_format_io(const rd_loadprof_io *d, uint64_t frame_ns, uint64_t at_ns, char *buf,
                          size_t n) {
  const unsigned long long avg = d->sd_reads ? (unsigned long long)(d->sd_read_bytes / d->sd_reads) : 0;
  return snprintf(buf, n,
                  "frame %llu ms, at %llu ms | engine: fopen %llu, fread %llu, fseek %llu, "
                  "read %llu, %llu KB in %llu ms | SD: open %llu in %llu ms, read %llu x avg "
                  "%llu B = %llu KB in %llu ms | cache: hit %llu, miss %llu | write %llu, "
                  "%llu KB in %llu ms | commit %llu in %llu ms",
                  MS(frame_ns), MS(at_ns), (unsigned long long)d->fopens,
                  (unsigned long long)d->freads, (unsigned long long)d->fseeks,
                  (unsigned long long)d->reads, KB(d->read_bytes), MS(d->read_ns),
                  (unsigned long long)d->sd_opens, MS(d->sd_open_ns),
                  (unsigned long long)d->sd_reads, avg, KB(d->sd_read_bytes), MS(d->sd_read_ns),
                  (unsigned long long)d->cache_hits, (unsigned long long)d->cache_misses,
                  (unsigned long long)d->writes, KB(d->write_bytes), MS(d->write_ns),
                  (unsigned long long)d->commits, MS(d->commit_ns));
}

int rd_loadprof_format_cpu(const rd_loadprof_thread *before, int nb,
                           const rd_loadprof_thread *after, int na, uint64_t frame_ns,
                           char *buf, size_t n) {
  /* the CPU each thread used between the two samples */
  struct { int idx; uint64_t ns; } used[RD_LOADPROF_MAX_THREADS];
  int nu = 0;
  for (int i = 0; i < na && nu < RD_LOADPROF_MAX_THREADS; i++)
    for (int j = 0; j < nb; j++)
      if (before[j].tid == after[i].tid) {
        const uint64_t ns = after[i].cpu_ns >= before[j].cpu_ns ? after[i].cpu_ns - before[j].cpu_ns : 0;
        if (frame_ns && ns * 100 >= frame_ns) { used[nu].idx = i; used[nu].ns = ns; nu++; }
        break;
      }
  for (int i = 1; i < nu; i++)                       /* busiest first (insertion sort) */
    for (int k = i; k > 0 && used[k].ns > used[k - 1].ns; k--) {
      const __typeof__(used[0]) t = used[k]; used[k] = used[k - 1]; used[k - 1] = t;
    }
  size_t len = 0;
  int w = snprintf(buf, n, "cpu:");
  if (w > 0) len = (size_t)w;
  for (int i = 0; i < nu && i < RD_LOADPROF_TOP && len < n; i++) {
    w = snprintf(buf + len, n - len, " %s %llu ms (%llu%%)%s", after[used[i].idx].name,
                 MS(used[i].ns), (unsigned long long)(used[i].ns * 100 / frame_ns),
                 i + 1 < nu && i + 1 < RD_LOADPROF_TOP ? "," : "");
    if (w < 0) break;
    len += (size_t)w;
  }
  if (nu == 0 && len < n) {
    w = snprintf(buf + len, n - len, " no thread above 1%%");
    if (w > 0) len += (size_t)w;
  }
  return (int)len;
}
