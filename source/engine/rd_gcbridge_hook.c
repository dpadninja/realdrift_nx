/* rd_gcbridge_hook.c -- Boehm GC stop-the-world on Horizon: pthread_kill for the collector's
 * suspend signal, done from outside the target thread (see rd_gcbridge.h).
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE


#include "rd_gcbridge.h"
#include "util.h"
#include "libc_shim.h"
#include "diag.h"
#include "rd_modimage.h"
#include "rd_log.h"
#include "rd_perfstat.h"


/* --- Boehm GC stop-the-world bridge -------------------------------------
 * il2cpp's Boehm GC stops the world by sending every other thread a suspend signal via
 * pthread_kill; each target's handler records its registers/stack top, sem_posts an ack and
 * parks until the restart signal. POSIX signals are never delivered on Switch, so without
 * help the first collection hangs in GC_stop_world.
 *
 * pthread_kill_gc (below) does the handler's work for it: it pauses the target with
 * svcSetThreadActivity, publishes its registers and stack pointer into the collector's
 * GC_threads entry and posts the ack (rd_gcbridge.h). The GC globals it needs are FOUND in
 * libil2cpp by instruction pattern (rd_patchsites.c rd_find_gc_vars); until that succeeds
 * the bridge stays inert. analysis/research/08-device-runs.md (runs 09-12) has the history. */
uintptr_t g_il2cpp_base = 0;
size_t    g_il2cpp_size = 0;   /* mapped size of libil2cpp; 0 = unknown (guard inert) */

static rd_gc_vars g_gcv;
static int        g_gc_ok = 0;

int rd_gc_bridge_init(const so_module *il2cpp) {
  rd_image im;
  int r = rd_image_from_module(il2cpp, &im) == 0 ? rd_find_gc_vars(&im, &g_gcv) : RD_SITE_NOT_FOUND;
  g_gc_ok = r == RD_SITE_OK;
  if (g_gc_ok)
    debugPrintf(RD_LOG_TAG_GC " stop-the-world bridge ARMED (suspend sig@+0x%llx restart sig@+0x%llx "
                "retry@+0x%llx ack sem@+0x%llx stop_count@+0x%llx threads@+0x%llx)\n",
                (unsigned long long)g_gcv.sig_suspend, (unsigned long long)g_gcv.sig_restart,
                (unsigned long long)g_gcv.retry_signals, (unsigned long long)g_gcv.suspend_ack_sem,
                (unsigned long long)g_gcv.stop_count, (unsigned long long)g_gcv.threads);
  else
    debugPrintf(RD_LOG_TAG_GC " stop-the-world bridge DISABLED: GC patterns not found (result %d)\n", r);
  return g_gc_ok;
}

#define GC_SUSPEND_SIG_OFF g_gcv.sig_suspend
#define GC_RESTART_SIG_OFF g_gcv.sig_restart
#define GC_START_ACK_OFF   g_gcv.retry_signals
#define GC_ACK_SEM_OFF     g_gcv.suspend_ack_sem

/* Largest offset this bridge dereferences (any of the globals, + the widest access). */
static uint64_t gc_max_off(void) {
  const uint64_t a[] = { g_gcv.sig_suspend, g_gcv.sig_restart, g_gcv.retry_signals,
                         g_gcv.suspend_ack_sem, g_gcv.stop_count,
                         g_gcv.threads + 8ull * RD_GC_THREAD_TABLE_SZ };
  uint64_t m = 0;
  for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) if (a[i] > m) m = a[i];
  return m + sizeof(void *);
}
#define GC_MAX_OFF gc_max_off()

/* Real stop-the-world (rd_gcbridge.h). A suspended thread is paused with
 * svcSetThreadActivity and its registers/stack are handed to the collector, mirroring
 * GC_suspend_handler_inner; restart resumes it. If anything fails we fall back to the old
 * ack-only behaviour (thread keeps running, logged). Run 12: ack-only let the collector free
 * objects held by the running timer thread -> "Unable to cast object" crashes. */
static uint64_t gc_mem_rd(void *c, uint64_t a) { (void)c; return *(volatile uint64_t *)(uintptr_t)a; }
static void     gc_mem_wr(void *c, uint64_t a, uint64_t v) { (void)c; *(volatile uint64_t *)(uintptr_t)a = v; }
static const rd_mem g_gc_mem = { gc_mem_rd, gc_mem_wr, NULL };

#define GC_MAX_PAUSED 96   /* max threads paused by one stop-the-world */
static struct { pthread_t pth; Handle h; uint64_t entry; } g_gc_paused[GC_MAX_PAUSED];
static int g_gc_npaused = 0;
static unsigned g_gc_stops = 0, g_gc_fallbacks = 0, g_gc_logged = 0;

/* Pauses thread t (never the calling thread), reads its registers and publishes them on its
 * stack as Boehm's suspend handler would (rd_gc_emulate_suspend); returns 1 if t is now
 * stopped, 0 if it could not be paused. */
static int gc_pause_and_publish(pthread_t t, uint64_t entry, uint64_t stop) {
  Handle h = diag_handle_for_pthread(t);
  if (!h || h == threadGetCurHandle() || g_gc_npaused >= GC_MAX_PAUSED) return 0;
  if (R_FAILED(svcSetThreadActivity(h, ThreadActivity_Paused))) return 0;
  ThreadContext ctx;
  if (R_FAILED(svcGetThreadContext3(&ctx, h))) {
    svcSetThreadActivity(h, ThreadActivity_Runnable);
    return 0;
  }
  uint64_t regs[RD_GC_NREGS];
  for (int i = 0; i < RD_GC_NGPRS; i++) regs[i] = ctx.cpu_gprs[i].x;
  regs[RD_GC_REG_FP] = ctx.fp;
  regs[RD_GC_REG_LR] = ctx.lr;
  rd_gc_emulate_suspend(&g_gc_mem, entry, stop, ctx.sp, regs);
  g_gc_paused[g_gc_npaused].pth = t;
  g_gc_paused[g_gc_npaused].h = h;
  g_gc_paused[g_gc_npaused].entry = entry;
  g_gc_npaused++;
  return 1;
}

int pthread_kill_gc(pthread_t t, int sig) {
  /* Inert until rd_gc_bridge_init() validated the sites. */
  if (!g_gc_ok) return 0;
  uintptr_t b = g_il2cpp_base;
  if (b && g_il2cpp_size && GC_MAX_OFF > g_il2cpp_size) {
    static int logged_oob = 0;
    if (!logged_oob) { logged_oob = 1;
      debugPrintf(RD_LOG_TAG_GC " BRIDGE DISABLED: offsets exceed libil2cpp size (max 0x%x > 0x%zx)\n",
                  (unsigned)GC_MAX_OFF, g_il2cpp_size); }
    return 0;
  }
  if (!b || !sig) return 0;
  int suspend_sig = *(volatile int *)(b + GC_SUSPEND_SIG_OFF);
  int restart_sig = *(volatile int *)(b + GC_RESTART_SIG_OFF);
  void **ack_sem  = (void **)(b + GC_ACK_SEM_OFF);
  uint64_t stop   = *(volatile uint64_t *)(b + g_gcv.stop_count);

  if (sig == suspend_sig) {
    nx_stat_gcstop++;
    uint64_t entry = rd_gc_lookup_thread(&g_gc_mem, b + g_gcv.threads, (uint64_t)(uintptr_t)t);
    if (entry && rd_gc_already_stopped(&g_gc_mem, entry, stop))
      return 0;                                    /* duplicate signal: handler ignores it */
    if (!entry || !gc_pause_and_publish(t, entry, stop)) g_gc_fallbacks++;
    g_gc_stops++;
    sem_post_fake(ack_sem);
    return 0;
  }
  if (sig == restart_sig) {
    for (int i = 0; i < g_gc_npaused; i++) {
      if (!pthread_equal(g_gc_paused[i].pth, t)) continue;
      rd_gc_emulate_restart(&g_gc_mem, g_gc_paused[i].entry, stop);
      svcSetThreadActivity(g_gc_paused[i].h, ThreadActivity_Runnable);
      g_gc_paused[i] = g_gc_paused[--g_gc_npaused];
      break;
    }
    if (*(volatile int *)(b + GC_START_ACK_OFF)) sem_post_fake(ack_sem);
    if (g_gc_npaused == 0 && g_gc_logged < 3) {    /* end of a stop-the-world episode */
      g_gc_logged++;
      debugPrintf(RD_LOG_TAG_GC " stop-the-world #%llu: %u thread(s) stopped, %u ack-only fallback(s)\n",
                  (unsigned long long)(stop >> 1), g_gc_stops, g_gc_fallbacks);
      g_gc_stops = g_gc_fallbacks = 0;
    }
    return 0;
  }
  return 0;   /* any other signal: no-op, as before */
}
