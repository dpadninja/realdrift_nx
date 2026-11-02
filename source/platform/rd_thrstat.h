/* rd_thrstat.h -- config.txt debug_threads: where the CPU goes, thread by thread and core by
 * core, and how often the engine's threads wait and wake through our pthread / futex shims.
 *
 * Every RD_THRSTAT_WINDOW_NS the loader samples each registered engine thread (priority, ideal
 * core, affinity mask, CPU time on each core: svcGetInfo ThreadTickCount per core) and the
 * idle time of core 0, and logs "[thr]" lines with the difference: the frames of the window,
 * the load of each core, the busiest threads, and the wait / wake rates. Engine threads are
 * created by libnx's pthread_create at priority 0x3B on the process's default core (0), below
 * the main loop's thread (0x2C) on the same core: these lines show what that costs.
 * Pure logic, host-tested (tests/c/test_rd_thrstat.c); diag.c and rd_thrstat_hook.c fill it.
 * MIT (see LICENSE). */
#ifndef RD_THRSTAT_H
#define RD_THRSTAT_H

#include <stddef.h>
#include <stdint.h>

#define RD_THRSTAT_CORES       4
#define RD_THRSTAT_MAX_THREADS 96
#define RD_THRSTAT_TOP         12     /* busiest threads listed */

/* One thread's sample: identity, scheduling, cumulative CPU time per core. */
typedef struct {
  uint64_t tid;
  char     name[32];
  int32_t  prio;                       /* Horizon priority: lower runs first */
  int32_t  ideal;                      /* preferred core (-1 unknown) */
  uint64_t mask;                       /* cores it may run on */
  uint64_t core_ns[RD_THRSTAT_CORES];  /* CPU time on each core so far */
} rd_thrstat_thread;

/* Wait / wake counters of the sync shims (cumulative; a window's share is a difference). */
typedef struct {
  uint64_t sem_timedwaits;   /* sem_timedwait calls */
  uint64_t sem_polls;        /* 1 ms sleeps inside them */
  uint64_t sem_timeouts;     /* calls that gave up */
  uint64_t sem_waits;        /* untimed sem_wait calls */
  uint64_t sem_posts;        /* sem_post calls */
  uint64_t mutex_polls;      /* 1 ms sleeps in pthread_mutex_timedlock */
  uint64_t cond_waits;       /* pthread_cond_wait / timedwait calls */
  uint64_t cond_capped;      /* untimed cond waits woken by the 16 ms cap */
  uint64_t futex_waits;      /* FUTEX_WAIT calls that slept */
  uint64_t futex_capped;     /* untimed futex waits woken by the 16 ms cap */
  uint64_t futex_wakes;      /* FUTEX_WAKE calls */
  uint64_t yields;           /* sched_yield calls */
} rd_thrstat_sync;

/* Frames of a window, from the main loop. */
typedef struct {
  uint32_t frames;
  uint32_t slow;             /* frames of RD_SLOW_FRAME_MS or more */
  uint64_t sum_ns, max_ns;   /* engine frame time (Unity_nativeRender and our per-frame work) */
} rd_thrstat_frames;

/* Adds one frame of frame_ns to f. */
void rd_thrstat_frame_add(rd_thrstat_frames *f, uint64_t frame_ns);

/* d = b - a, field by field. */
void rd_thrstat_sync_diff(const rd_thrstat_sync *a, const rd_thrstat_sync *b,
                          rd_thrstat_sync *d);

/* The window line (no prefix, no newline): its length, the frames (count, average and longest
 * time, slow ones) and core 0's idle share (idle_ns of window_ns; idle_ns > window_ns means not
 * measured). Returns snprintf's result. */
int rd_thrstat_format_window(uint64_t window_ns, const rd_thrstat_frames *f, uint64_t idle_ns,
                             char *buf, size_t n);

/* The cores line: the CPU time the threads of `after` used on each core since `before` (same
 * tid), as a share of window_ns. Returns snprintf's result. */
int rd_thrstat_format_cores(const rd_thrstat_thread *before, int nb,
                            const rd_thrstat_thread *after, int na, uint64_t window_ns,
                            char *buf, size_t n);

/* Fills order[0..max) with indexes into `after` of the threads that used at least 1% of
 * window_ns since `before`, busiest first; returns how many. A thread new in the window counts
 * from 0. */
int rd_thrstat_busiest(const rd_thrstat_thread *before, int nb, const rd_thrstat_thread *after,
                       int na, uint64_t window_ns, int *order, int max);

/* One thread's line: name, priority, ideal core, mask, then its share of window_ns in total and
 * on each core it used. `before` may be NULL (a new thread). Returns snprintf's result. */
int rd_thrstat_format_thread(const rd_thrstat_thread *before, const rd_thrstat_thread *after,
                             uint64_t window_ns, char *buf, size_t n);

/* The sync line: the window's counters d as rates per second. Returns snprintf's result. */
int rd_thrstat_format_sync(const rd_thrstat_sync *d, uint64_t window_ns, char *buf, size_t n);

#endif
