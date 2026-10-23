/* rd_jobs.c -- see rd_jobs.h. libnx threads pinned to cores 1 and 2; parts are handed out with
 * an atomic counter, so a slow core takes fewer of them. MIT (see LICENSE). */
#include <switch.h>
#include "rd_jobs.h"
#include "util.h"

#define WORKER_STACK 0x10000
#define WORKER_PRIO  0x2C          /* like the service thread (rd_patches.c); Horizon: lower = higher */

static struct {
  rd_job_fn fn;
  void *ctx;
  int parts;
  int next;                       /* the next part to take (atomic) */
} g_job;

static Thread    g_thr[RD_JOBS_MAX_WORKERS];
static int       g_workers = -1;  /* started workers; -1 = not tried yet */
static Semaphore g_start, g_done;
static Mutex     g_run_lock;      /* one job at a time */

/* Takes parts of the current job until none is left. */
static void work(void) {
  for (;;) {
    const int i = __atomic_fetch_add(&g_job.next, 1, __ATOMIC_ACQ_REL);
    if (i >= g_job.parts) return;
    g_job.fn(g_job.ctx, i, g_job.parts);
  }
}

static void worker_main(void *arg) {
  (void)arg;
  for (;;) {
    semaphoreWait(&g_start);
    work();
    semaphoreSignal(&g_done);
  }
}

/* Starts a worker on each of cores 1 and 2 the process may use (InfoType_CoreMask). */
static void start_workers(void) {
  semaphoreInit(&g_start, 0);
  semaphoreInit(&g_done, 0);
  u64 mask = 0;
  if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) mask = 0;
  g_workers = 0;
  for (int core = 1; core <= RD_JOBS_MAX_WORKERS; core++) {
    if (!(mask & (1ull << core))) continue;
    Thread *t = &g_thr[g_workers];
    if (R_FAILED(threadCreate(t, worker_main, NULL, NULL, WORKER_STACK, WORKER_PRIO, core))) continue;
    if (R_FAILED(threadStart(t))) { threadClose(t); continue; }
    g_workers++;
  }
  debugPrintf("[jobs] %d worker thread(s) (core mask 0x%llx)\n", g_workers,
              (unsigned long long)mask);
}

int rd_jobs_run(rd_job_fn fn, void *ctx, int parts) {
  if (parts <= 0) return 0;
  mutexLock(&g_run_lock);
  if (g_workers < 0) start_workers();
  g_job.fn = fn;
  g_job.ctx = ctx;
  g_job.parts = parts;
  __atomic_store_n(&g_job.next, 0, __ATOMIC_RELEASE);
  const int helpers = parts > 1 ? (g_workers < parts - 1 ? g_workers : parts - 1) : 0;
  for (int i = 0; i < helpers; i++) semaphoreSignal(&g_start);
  work();
  for (int i = 0; i < helpers; i++) semaphoreWait(&g_done);
  mutexUnlock(&g_run_lock);
  return helpers + 1;
}
