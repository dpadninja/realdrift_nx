/* rd_loadprof.h -- config.txt debug_load: where a long (loading) frame spends its time.
 *
 * Two "[load]" lines per frame of at least RD_LOAD_PROFILE_NS: the file traffic -- what the
 * engine asked for (fopen / fread / fseek / read) against what reached the SD card (FS opens
 * and reads, their bytes and time) -- and the CPU time each engine thread used in the frame.
 * Reading 128 KB stdio buffers for small reads, or many opens, show up as SD bytes far above
 * the engine's; one thread with nearly all the CPU means the load cannot gain from more
 * cores, several busy ones on the same core (all engine threads run on core 0) means it can.
 * Pure logic, host-tested (tests/c/test_rd_loadprof.c); main.c and diag.c fill it.
 * MIT (see LICENSE). */
#ifndef RD_LOADPROF_H
#define RD_LOADPROF_H

#include <stddef.h>
#include <stdint.h>

#define RD_LOADPROF_MAX_THREADS 96
#define RD_LOADPROF_TOP         8      /* busiest threads listed */

/* File traffic counters (cumulative; a frame's share is the difference of two). */
typedef struct {
  uint64_t fopens, freads, fseeks, reads;          /* engine calls */
  uint64_t read_bytes, read_ns;                    /* what fread / read returned, and the time */
  uint64_t sd_opens, sd_open_ns;                   /* FS file opens */
  uint64_t sd_reads, sd_read_bytes, sd_read_ns;    /* FS file reads */
} rd_loadprof_io;

/* One thread's cumulative CPU time. */
typedef struct {
  uint64_t tid;
  char name[32];
  uint64_t cpu_ns;
} rd_loadprof_thread;

/* d = b - a, field by field. */
void rd_loadprof_io_diff(const rd_loadprof_io *a, const rd_loadprof_io *b, rd_loadprof_io *d);

/* The file-traffic line (no prefix, no newline) for a frame of frame_ns with traffic d.
 * Returns snprintf's result. */
int rd_loadprof_format_io(const rd_loadprof_io *d, uint64_t frame_ns, char *buf, size_t n);

/* The CPU line: each thread of `after` that also is in `before` (same tid) with the CPU time it
 * used between them, busiest first, at most RD_LOADPROF_TOP, as "name ms (pct%)" of frame_ns;
 * threads that used under 1% are left out. Returns snprintf's result. */
int rd_loadprof_format_cpu(const rd_loadprof_thread *before, int nb,
                           const rd_loadprof_thread *after, int na, uint64_t frame_ns,
                           char *buf, size_t n);

#endif
