/* rd_gcbridge.h -- Boehm GC stop-the-world on Horizon (no POSIX signals).
 * Host-testable (tests/c/test_rd_gcbridge.c).
 *
 * Boehm GC suspends threads with pthread_kill(sig_suspend); each thread's signal handler
 * (GC_suspend_handler_inner, libil2cpp+0x75b344) records its stack pointer in its
 * GC_threads entry, acks on GC_suspend_ack_sem and sleeps until the restart signal. Switch
 * never delivers those signals, so pthread_kill_gc (libc_shim.c) does that work FROM OUTSIDE:
 * it pauses the target with svcSetThreadActivity, reads its registers, copies them onto the
 * thread's stack below sp and points stack_ptr there, so the collector scans the registers
 * and the whole live stack (run 12: without this, objects held only by the timer thread were
 * collected while the thread kept running -> "Unable to cast object" crashes).
 *
 * The globals are located by instruction patterns (rd_patchsites.c rd_find_gc_vars).
 * MIT (see LICENSE). */
#ifndef RD_GCBRIDGE_H
#define RD_GCBRIDGE_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

typedef struct {
  uint64_t sig_suspend;     /* int  GC_sig_suspend        */
  uint64_t sig_restart;     /* int  GC_sig_thr_restart    */
  uint64_t retry_signals;   /* int  GC_retry_signals (start-world ack gate) */
  uint64_t suspend_ack_sem; /* sem_t GC_suspend_ack_sem   */
  uint64_t stop_count;      /* word GC_stop_count         */
  uint64_t threads;         /* GC_thread GC_threads[256]  */
} rd_gc_vars;               /* link-time addresses (RVAs) in libil2cpp.so */

/* ---- GC_threads entries (runtime addresses) ---- */
#define RD_GC_THR_NEXT       0x00
#define RD_GC_THR_ID         0x08
#define RD_GC_THR_LAST_STOP  0x10
#define RD_GC_THR_STACK_PTR  0x18
#define RD_GC_THREAD_TABLE_SZ 256         /* THREAD_TABLE_SZ in Boehm's pthread_support.h */
/* Bound on a hash chain walk: more threads than this cannot exist; stops a corrupt loop. */
#define RD_GC_MAX_CHAIN       4096

/* Registers saved for a stopped thread: x0..x28, fp (x29), lr (x30). */
#define RD_GC_NGPRS 29
#define RD_GC_REG_FP RD_GC_NGPRS
#define RD_GC_REG_LR (RD_GC_NGPRS + 1)
#define RD_GC_NREGS  (RD_GC_NGPRS + 2)
/* Gap left below the thread's sp before the saved-register block. */
#define RD_GC_SP_GAP 0x100

typedef struct {
  uint64_t (*rd)(void *ctx, uint64_t addr);
  void     (*wr)(void *ctx, uint64_t addr, uint64_t val);
  void      *ctx;
} rd_mem;

/* Bucket of `id` in GC_threads, exactly as GC_suspend_handler_inner computes it. */
unsigned rd_gc_hash(uint64_t id);

/* Address of the GC_threads entry for pthread `id`, or 0. `table` = runtime GC_threads. */
uint64_t rd_gc_lookup_thread(const rd_mem *m, uint64_t table, uint64_t id);

/* 1 if the entry already acknowledged stop number `stop_count` (handler would return). */
int rd_gc_already_stopped(const rd_mem *m, uint64_t entry, uint64_t stop_count);

/* Does what the suspend handler does for a thread paused at `sp` with registers `regs`:
 * saves the registers below sp, sets stack_ptr to them and last_stop_count = stop_count.
 * Returns the new stack_ptr. */
uint64_t rd_gc_emulate_suspend(const rd_mem *m, uint64_t entry, uint64_t stop_count,
                               uint64_t sp, const uint64_t regs[RD_GC_NREGS]);

/* What the handler does after the restart signal: last_stop_count = stop_count | 1. */
void rd_gc_emulate_restart(const rd_mem *m, uint64_t entry, uint64_t stop_count);

/* ---- glue (rd_gcbridge_hook.c) -------------------------------------------------------- */
struct so_module;
/* Finds the GC globals in libil2cpp.so by pattern; call after so_finalize. Returns 1 if the
 * bridge is live, 0 if the patterns did not match (GC then runs without the bridge). */
int rd_gc_bridge_init(const struct so_module *il2cpp);
/* pthread_kill for the engine: the GC's suspend/restart signals are carried out from outside
 * the target thread (paused with svcSetThreadActivity); other signals are ignored. Returns 0. */
int pthread_kill_gc(pthread_t t, int sig);
/* Where libil2cpp is mapped (set by main.c after so_finalize); bounds the stack/register
 * accesses of the bridge. g_il2cpp_size 0 = unknown, the bounds guard stays inert. */
extern uintptr_t g_il2cpp_base;
extern size_t    g_il2cpp_size;

#endif
