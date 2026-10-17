/* rd_gpuwait.c -- where a slow frame waits for the GPU or the display.
 *
 * mesa takes the next window buffer lazily (nwindowDequeueBuffer at the first draw of a frame,
 * not in eglSwapBuffers) and libdrm_nouveau waits for GPU work with nvFenceWait; both are
 * libnx calls inside the static mesa libraries, so the Makefile links them through
 * -Wl,--wrap and the time spent in them is counted here -> "deq=" / "fence=" in the [perf]
 * line (main.c). A slow frame with large deq/fence is waiting for the GPU; frame times in
 * multiples of 16.7 ms point the same way (buffers free up at vblank).
 *
 * rd_log_clocks() logs the clocks the console is really running at (overclock tools apply per
 * title, so this shows whether they reach this process). MIT (see LICENSE). */
#include <switch.h>
#include "libc_shim.h"   /* nx_stat_* */
#include "rd_platform.h"
#include "util.h"        /* debugPrintf */
#include "rd_gpuwait.h"

/* Linker hooks: -Wl,--wrap=X sends mesa's calls of X to __wrap_X; __real_X is libnx's X. */
Result __wrap_nwindowDequeueBuffer(NWindow *nw, s32 *out_slot, NvMultiFence *out_fence);
Result __wrap_nvFenceWait(NvFence *f, s32 timeout_us);
Result __wrap_nwindowSetSwapInterval(NWindow *nw, u32 swap_interval);

Result __real_nwindowDequeueBuffer(NWindow *nw, s32 *out_slot, NvMultiFence *out_fence);
Result __wrap_nwindowDequeueBuffer(NWindow *nw, s32 *out_slot, NvMultiFence *out_fence) {
  const uint64_t t0 = rd_now_ns();
  const Result r = __real_nwindowDequeueBuffer(nw, out_slot, out_fence);
  __atomic_add_fetch(&nx_stat_dequeue_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  return r;
}

Result __real_nvFenceWait(NvFence *f, s32 timeout_us);
Result __wrap_nvFenceWait(NvFence *f, s32 timeout_us) {
  const uint64_t t0 = rd_now_ns();
  const Result r = __real_nvFenceWait(f, timeout_us);
  __atomic_add_fetch(&nx_stat_fence_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  return r;
}

Result __real_nwindowSetSwapInterval(NWindow *nw, u32 swap_interval);
Result __wrap_nwindowSetSwapInterval(NWindow *nw, u32 swap_interval) {
  const Result r = __real_nwindowSetSwapInterval(nw, swap_interval);
  debugPrintf("[gfx] nwindowSetSwapInterval(%u) rc=0x%x\n", (unsigned)swap_interval, (unsigned)r);
  return r;
}

static unsigned clock_mhz(PcvModuleId module, Result *rc) {
  ClkrstSession s;
  u32 hz = 0;
  *rc = clkrstOpenSession(&s, module, 3);
  if (R_FAILED(*rc)) return 0;
  *rc = clkrstGetClockRate(&s, &hz);
  clkrstCloseSession(&s);
  return hz / 1000000u;
}

void rd_log_clocks(const char *when) {
  static int ready = -1;
  if (ready < 0) {
    const Result rc = clkrstInitialize();
    ready = R_SUCCEEDED(rc);
    if (!ready) { debugPrintf("[perf] clocks (%s): clkrst unavailable rc=0x%x\n", when, (unsigned)rc); return; }
  }
  if (!ready) return;
  Result rc_cpu, rc_gpu, rc_emc;
  const unsigned cpu = clock_mhz(PcvModuleId_CpuBus, &rc_cpu);
  const unsigned gpu = clock_mhz(PcvModuleId_GPU, &rc_gpu);
  const unsigned emc = clock_mhz(PcvModuleId_EMC, &rc_emc);
  debugPrintf("[perf] clocks (%s): CPU %u MHz, GPU %u MHz, EMC %u MHz (rc 0x%x/0x%x/0x%x)\n",
              when, cpu, gpu, emc, (unsigned)rc_cpu, (unsigned)rc_gpu, (unsigned)rc_emc);
}
