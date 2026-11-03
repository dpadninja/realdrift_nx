/* rd_thrstat_hook.c -- see rd_thrstat_hook.h. MIT (see LICENSE). */
#include <stdbool.h>
#include <string.h>
#include <switch.h>
#include "diag.h"
#include "rd_perfstat.h"
#include "rd_platform.h"
#include "rd_thrstat.h"
#include "rd_thrstat_hook.h"
#include "util.h"

/* Horizon gives a core's idle time only to a thread running on that core, so each core of the
 * process gets a sampler thread pinned to it. It sleeps until the main loop asks, reads the
 * idle time and answers: a few microseconds every RD_THRSTAT_WINDOW_NS. Its priority is above
 * the main loop's (0x2C) and the engine's (0x3B) so that it answers at once even on a core
 * those keep busy. */
#define SAMPLER_PRIO  0x28
#define SAMPLER_STACK 0x2000
#define SAMPLER_WAIT_NS (2 * RD_NS_PER_MS)   /* how long the main loop waits for the answers */

typedef struct {
  Thread   thr;
  UEvent   go, done;
  uint64_t idle_ns;     /* written by the sampler before `done`, read by the main loop after */
  bool     running;
} CoreSampler;

static CoreSampler g_samplers[RD_THRSTAT_CORES];
static uint64_t g_core_mask;          /* cores with a running sampler */

static rd_thrstat_thread g_a[RD_THRSTAT_MAX_THREADS], g_b[RD_THRSTAT_MAX_THREADS];
static rd_thrstat_thread *g_before = g_a, *g_after = g_b;
static int g_nb = -1;                 /* -1: no sample yet */
static rd_thrstat_sync g_sync;
static rd_thrstat_frames g_frames;
static uint64_t g_start_ns, g_idle[RD_THRSTAT_CORES];

/* Sampler thread: on each request, the idle time of the core it runs on. */
static void sampler_main(void *arg) {
  CoreSampler *s = arg;
  for (;;) {
    if (R_FAILED(waitSingle(waiterForUEvent(&s->go), UINT64_MAX))) continue;
    u64 ticks = 0;
    const u64 core = svcGetCurrentProcessorNumber();
    s->idle_ns = R_SUCCEEDED(svcGetInfo(&ticks, InfoType_IdleTickCount, INVALID_HANDLE, core))
                     ? armTicksToNs(ticks) : RD_THRSTAT_NO_IDLE;
    ueventSignal(&s->done);
  }
}

/* A sampler on each core the process may use; returns the cores that have one. */
static uint64_t start_samplers(void) {
  u64 mask = 0;
  if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) return 0;
  uint64_t started = 0;
  for (int c = 0; c < RD_THRSTAT_CORES; c++) {
    CoreSampler *s = &g_samplers[c];
    if (!(mask & (1ull << c))) continue;
    ueventCreate(&s->go, true);
    ueventCreate(&s->done, true);
    if (R_FAILED(threadCreate(&s->thr, sampler_main, s, NULL, SAMPLER_STACK, SAMPLER_PRIO, c)))
      continue;
    if (R_FAILED(threadStart(&s->thr))) { threadClose(&s->thr); continue; }
    s->running = true;
    started |= 1ull << c;
  }
  return started;
}

/* Fills idle[c] with each core's idle time so far (RD_THRSTAT_NO_IDLE: no answer). Asks every
 * sampler first, then collects, so the cores are read within microseconds of each other. */
static void sample_idle(uint64_t idle[RD_THRSTAT_CORES]) {
  for (int c = 0; c < RD_THRSTAT_CORES; c++) {
    idle[c] = RD_THRSTAT_NO_IDLE;
    if (!g_samplers[c].running) continue;
    ueventClear(&g_samplers[c].done);   /* a late answer to the last request is not this one */
    ueventSignal(&g_samplers[c].go);
  }
  for (int c = 0; c < RD_THRSTAT_CORES; c++) {
    CoreSampler *s = &g_samplers[c];
    if (s->running && R_SUCCEEDED(waitSingle(waiterForUEvent(&s->done), SAMPLER_WAIT_NS)))
      idle[c] = s->idle_ns;
  }
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
              "on core %u; idle samplers on cores 0x%llx\n", (unsigned long long)mask,
              (unsigned)prio, (int)ideal, (unsigned long long)tmask,
              (unsigned)svcGetCurrentProcessorNumber(), (unsigned long long)g_core_mask);
}

void rd_thrstat_frame(uint64_t frame_ns) {
  const uint64_t now = rd_now_ns();
  if (g_nb < 0) {
    g_core_mask = start_samplers();
    log_setup();
    g_nb = diag_thread_sample(g_before, RD_THRSTAT_MAX_THREADS);
    sync_sample(&g_sync);
    sample_idle(g_idle);
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
  uint64_t idle[RD_THRSTAT_CORES], idle_d[RD_THRSTAT_CORES];
  sample_idle(idle);
  for (int c = 0; c < RD_THRSTAT_CORES; c++)
    idle_d[c] = idle[c] != RD_THRSTAT_NO_IDLE && g_idle[c] != RD_THRSTAT_NO_IDLE &&
                idle[c] >= g_idle[c] ? idle[c] - g_idle[c] : RD_THRSTAT_NO_IDLE;

  char line[640];
  rd_thrstat_format_window(window, &g_frames, line, sizeof line);
  debugPrintf("[thr] %s\n", line);
  rd_thrstat_format_cores(g_core_mask, idle_d, window, line, sizeof line);
  debugPrintf("[thr] %s\n", line);
  rd_thrstat_format_masks(g_before, g_nb, g_after, na, window, line, sizeof line);
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
  memcpy(g_idle, idle, sizeof g_idle);
  g_start_ns = now;
  memset(&g_frames, 0, sizeof g_frames);
}
