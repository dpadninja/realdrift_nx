/* rd_perfstat.h -- frame-spike counters.
 *
 * Cheap counters bumped by hot paths in several layers (file opens, SD commits, mmaps, GC stops,
 * read volume, time waiting for the GPU or the display). main.c times each frame and, when one
 * is slow, prints the DELTA of these since the frame began in its [perf] line -- so a hitch names
 * its own cause. The time counters are updated with __atomic_add_fetch (they may be bumped from
 * engine threads); the others are plain ints, single writer in practice, read for magnitudes.
 * MIT (see LICENSE). */
#ifndef RD_PERFSTAT_H
#define RD_PERFSTAT_H

/* Counts: open() calls, SD commits, mmap calls, GC stop-the-world pauses. */
extern unsigned nx_stat_open, nx_stat_commit, nx_stat_mmap, nx_stat_gcstop;
/* Bytes / time spent in read() + fread() (loading profile). */
extern volatile unsigned long long nx_stat_read_bytes, nx_stat_read_ns;
/* debug_load (rd_loadprof.h): the engine's stdio / read calls, and what reached the SD card
 * (FS file opens and reads, counted in platform/rd_sdstat.c). Atomic. */
extern volatile unsigned long long nx_stat_fopen, nx_stat_fread, nx_stat_fseek, nx_stat_read;
extern volatile unsigned long long nx_stat_sd_open, nx_stat_sd_open_ns;
extern volatile unsigned long long nx_stat_sd_read, nx_stat_sd_read_bytes, nx_stat_sd_read_ns;
/* debug_load: write() / fwrite() calls, bytes and time; time in SD commits (fsdevCommitDevice,
 * counted by nx_stat_commit); the time main() began (rd_now_ns). */
extern volatile unsigned long long nx_stat_write, nx_stat_write_bytes, nx_stat_write_ns;
extern volatile unsigned long long nx_stat_commit_ns, nx_stat_start_ns;
/* archive_cache block lookups served from memory / read from the card (bionic/shim_fs_cache.c). */
extern volatile unsigned long long nx_stat_cache_hit, nx_stat_cache_miss;
/* archive reads served by the launch prefetch (rd_readlog.h). */
extern volatile unsigned long long nx_stat_readlog_hit;
/* Time in eglSwapBuffers, and in GL calls that can wait for the GPU (android/egl). */
extern volatile unsigned long long nx_stat_swap_ns, nx_stat_glwait_ns;
/* Time mesa waits for a free window buffer / for GPU fences (platform/rd_gpuwait.c). */
extern volatile unsigned long long nx_stat_dequeue_ns, nx_stat_fence_ns;

#endif
