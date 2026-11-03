/* rd_thrstat.h -- config.txt debug_threads: where the CPU goes, thread by thread and core by
 * core, and how often the engine's threads wait and wake through our pthread / futex shims.
 *
 * Every RD_THRSTAT_WINDOW_NS the loader samples each registered engine thread (priority, ideal
 * core, affinity mask, CPU time on all cores) and the idle time of each core of the process,
 * and logs "[thr]" lines with the difference: the frames of the window, the load of each core,
 * the engine threads' CPU grouped by affinity mask, the busiest threads, and the wait / wake
 * rates. Engine threads are created by libnx's pthread_create at priority 0x3B on the
 * process's default core (0), below the main loop's thread (0x2C): these lines show what that
 * costs.
 * Horizon reports a core's idle time (IdleTickCount) and a thread's time on one core
 * (ThreadTickCount with a core number) only to a thread running on that core: a thread's time
 * is therefore its total over all cores, and each core's idle time is read by a sampler thread
 * pinned to that core (rd_thrstat_hook.c).
 * Pure logic, host-tested (tests/c/test_rd_thrstat.c); diag.c and rd_thrstat_hook.c fill it.
 * MIT (see LICENSE). */
#ifndef RD_THRSTAT_H
#define RD_THRSTAT_H

#include <stddef.h>
#include <stdint.h>

#define RD_THRSTAT_CORES       4
#define RD_THRSTAT_MAX_THREADS 96
#define RD_THRSTAT_TOP         12     /* busiest threads listed */
#define RD_THRSTAT_MASKS       8      /* distinct affinity masks summed in the masks line */
#define RD_THRSTAT_NO_IDLE     UINT64_MAX  /* a core's idle time that was not measured */

/* One thread's sample: identity, scheduling, cumulative CPU time on all cores. */
typedef struct {
  uint64_t tid;
  char     name[32];
  int32_t  prio;                       /* Horizon priority: lower runs first */
  int32_t  ideal;                      /* preferred core (-1 unknown) */
  uint64_t mask;                       /* cores it may run on */
  uint64_t cpu_ns;                     /* CPU time on all cores so far */
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

/* The window line (no prefix, no newline): its length and the frames (count, average and
 * longest time, slow ones). Returns snprintf's result. */
int rd_thrstat_format_window(uint64_t window_ns, const rd_thrstat_frames *f, char *buf,
                             size_t n);

/* The cores line: the busy share of window_ns of each core in `mask` (the process's cores),
 * from idle_ns[c], the core's idle time in the window (all threads of the process and of the
 * system). A core whose idle time is RD_THRSTAT_NO_IDLE or above window_ns shows "-".
 * Returns snprintf's result. */
int rd_thrstat_format_cores(uint64_t mask, const uint64_t idle_ns[RD_THRSTAT_CORES],
                            uint64_t window_ns, char *buf, size_t n);

/* The masks line: how many engine threads `after` holds and the CPU time they used since
 * `before` (same tid) as a share of window_ns, in total and per affinity mask (up to
 * RD_THRSTAT_MASKS masks, in order of first appearance; the rest is counted as "other").
 * Returns snprintf's result. */
int rd_thrstat_format_masks(const rd_thrstat_thread *before, int nb,
                            const rd_thrstat_thread *after, int na, uint64_t window_ns,
                            char *buf, size_t n);

/* Fills order[0..max) with indexes into `after` of the threads that used at least 1% of
 * window_ns since `before`, busiest first; returns how many. A thread new in the window counts
 * from 0. */
int rd_thrstat_busiest(const rd_thrstat_thread *before, int nb, const rd_thrstat_thread *after,
                       int na, uint64_t window_ns, int *order, int max);

/* One thread's line: name, priority, ideal core, mask, then its CPU share of window_ns.
 * `before` may be NULL (a new thread). Returns snprintf's result. */
int rd_thrstat_format_thread(const rd_thrstat_thread *before, const rd_thrstat_thread *after,
                             uint64_t window_ns, char *buf, size_t n);

/* The sync line: the window's counters d as rates per second. Returns snprintf's result. */
int rd_thrstat_format_sync(const rd_thrstat_sync *d, uint64_t window_ns, char *buf, size_t n);

#endif
