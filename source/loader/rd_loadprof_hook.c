/* rd_loadprof_hook.c -- see rd_loadprof_hook.h. MIT (see LICENSE). */
#include "diag.h"
#include "rd_loadprof.h"
#include "rd_loadprof_hook.h"
#include "rd_perfstat.h"
#include "rd_platform.h"
#include "util.h"

static rd_loadprof_io g_io;
static rd_loadprof_thread g_before[RD_LOADPROF_MAX_THREADS], g_after[RD_LOADPROF_MAX_THREADS];
static int g_nb;

#define LOAD(x) __atomic_load_n(&(x), __ATOMIC_RELAXED)

static void io_sample(rd_loadprof_io *s) {
  *s = (rd_loadprof_io){
    .fopens = LOAD(nx_stat_fopen), .freads = LOAD(nx_stat_fread),
    .fseeks = LOAD(nx_stat_fseek), .reads = LOAD(nx_stat_read),
    .read_bytes = LOAD(nx_stat_read_bytes), .read_ns = LOAD(nx_stat_read_ns),
    .sd_opens = LOAD(nx_stat_sd_open), .sd_open_ns = LOAD(nx_stat_sd_open_ns),
    .sd_reads = LOAD(nx_stat_sd_read), .sd_read_bytes = LOAD(nx_stat_sd_read_bytes),
    .sd_read_ns = LOAD(nx_stat_sd_read_ns),
    .cache_hits = LOAD(nx_stat_cache_hit), .cache_misses = LOAD(nx_stat_cache_miss),
    .writes = LOAD(nx_stat_write), .write_bytes = LOAD(nx_stat_write_bytes),
    .write_ns = LOAD(nx_stat_write_ns),
    .commits = LOAD(nx_stat_commit), .commit_ns = LOAD(nx_stat_commit_ns),
  };
}

void rd_loadprof_begin(void) {
  io_sample(&g_io);
  g_nb = diag_thread_cpu(g_before, RD_LOADPROF_MAX_THREADS);
}

void rd_loadprof_end(uint64_t frame_ns) {
  if (frame_ns < RD_LOAD_PROFILE_NS) return;
  rd_loadprof_io now, d;
  io_sample(&now);
  rd_loadprof_io_diff(&g_io, &now, &d);
  const int na = diag_thread_cpu(g_after, RD_LOADPROF_MAX_THREADS);
  char line[640];
  rd_loadprof_format_io(&d, frame_ns, rd_now_ns() - LOAD(nx_stat_start_ns), line, sizeof line);
  debugPrintf("[load] %s\n", line);
  rd_loadprof_format_cpu(g_before, g_nb, g_after, na, frame_ns, line, sizeof line);
  debugPrintf("[load] %s\n", line);
}
