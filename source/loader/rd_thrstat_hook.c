/* rd_thrstat_hook.c -- see rd_thrstat_hook.h. MIT (see LICENSE). */
#include <string.h>
#include <switch.h>
#include "diag.h"
#include "rd_perfstat.h"
#include "rd_platform.h"
#include "rd_thrstat.h"
#include "rd_thrstat_hook.h"
#include "util.h"

static rd_thrstat_thread g_a[RD_THRSTAT_MAX_THREADS], g_b[RD_THRSTAT_MAX_THREADS];
static rd_thrstat_thread *g_before = g_a, *g_after = g_b;
static int g_nb = -1;                 /* -1: no sample yet */
static rd_thrstat_sync g_sync;
static rd_thrstat_frames g_frames;
static uint64_t g_start_ns, g_idle0;

/* Idle ticks of core 0 so far, or UINT64_MAX when this thread is not on core 0 (Horizon only
 * reports the idle time of the caller's own core). */
static uint64_t core0_idle_ns(void) {
  u64 ticks = 0;
  if (svcGetCurrentProcessorNumber() != 0 ||
      R_FAILED(svcGetInfo(&ticks, InfoType_IdleTickCount, INVALID_HANDLE, TickCountInfo_Total)))
    return UINT64_MAX;
  return armTicksToNs(ticks);
}

static void sync_sample(rd_thrstat_sync *s) {
  const uint64_t *src = (const uint64_t *)&nx_stat_sync;
  uint64_t *dst = (uint64_t *)s;
  for (size_t i = 0; i < sizeof *s / sizeof(uint64_t); i++)
    dst[i] = __atomic_load_n(&src[i], __ATOMIC_RELAXED);
}

/* The process's cores and the main thread's scheduling, once. */
static void log_setup(void) {
  u64 mask = 0;
  s32 prio = -1, ideal = -1;
  u64 tmask = 0;
  svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
  svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  svcGetThreadCoreMask(&ideal, &tmask, CUR_THREAD_HANDLE);
  debugPrintf("[thr] process cores 0x%llx; main loop thread prio 0x%02x ideal %d mask 0x%llx, "
              "on core %u\n", (unsigned long long)mask, (unsigned)prio, (int)ideal,
              (unsigned long long)tmask, (unsigned)svcGetCurrentProcessorNumber());
}

void rd_thrstat_frame(uint64_t frame_ns) {
  const uint64_t now = rd_now_ns();
  if (g_nb < 0) {
    log_setup();
    g_nb = diag_thread_sample(g_before, RD_THRSTAT_MAX_THREADS);
    sync_sample(&g_sync);
    g_idle0 = core0_idle_ns();
    g_start_ns = now;
    return;
  }
  rd_thrstat_frame_add(&g_frames, frame_ns);
  const uint64_t window = now - g_start_ns;
  if (window < RD_THRSTAT_WINDOW_NS) return;

  const int na = diag_thread_sample(g_after, RD_THRSTAT_MAX_THREADS);
  rd_thrstat_sync sync, d;
  sync_sample(&sync);
  rd_thrstat_sync_diff(&g_sync, &sync, &d);
  const uint64_t idle0 = core0_idle_ns();
  const uint64_t idle = idle0 != UINT64_MAX && g_idle0 != UINT64_MAX && idle0 >= g_idle0
                            ? idle0 - g_idle0 : UINT64_MAX;

  char line[640];
  rd_thrstat_format_window(window, &g_frames, idle, line, sizeof line);
  debugPrintf("[thr] %s\n", line);
  rd_thrstat_format_cores(g_before, g_nb, g_after, na, window, line, sizeof line);
  debugPrintf("[thr] %s\n", line);
  int order[RD_THRSTAT_TOP];
  const int k = rd_thrstat_busiest(g_before, g_nb, g_after, na, window, order, RD_THRSTAT_TOP);
  for (int i = 0; i < k; i++) {
    const rd_thrstat_thread *a = &g_after[order[i]], *b = NULL;
    for (int j = 0; j < g_nb && !b; j++)
      if (g_before[j].tid == a->tid) b = &g_before[j];
    rd_thrstat_format_thread(b, a, window, line, sizeof line);
    debugPrintf("[thr]   %s\n", line);
  }
  rd_thrstat_format_sync(&d, window, line, sizeof line);
  debugPrintf("[thr] %s\n", line);

  rd_thrstat_thread *t = g_before;   /* this window's end is the next one's start */
  g_before = g_after;
  g_after = t;
  g_nb = na;
  g_sync = sync;
  g_idle0 = idle0;
  g_start_ns = now;
  memset(&g_frames, 0, sizeof g_frames);
}
