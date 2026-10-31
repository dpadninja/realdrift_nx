/* rd_gcload_hook.c -- see rd_gcload.h: the il2cpp glue. MIT (see LICENSE). */
#include <stdint.h>
#include "rd_gcload.h"
#include "rd_il2cpp.h"
#include "rd_log.h"
#include "rd_platform.h"
#include "util.h"

#define MB(x) ((long long)((x) >> 20))

static void (*real_collect)(int);

static void w_collect(int max_generations) {
  const rd_il2cpp_api *a = &g_il2cpp;
  if (!a->gc_is_disabled || !a->gc_enable || !a->gc_disable || !a->gc_get_used_size ||
      !a->gc_get_heap_size) {          /* before main.c resolved the API: as before */
    real_collect(max_generations);
    return;
  }
  const rd_gc_ops ops = { a->gc_is_disabled, a->gc_enable, a->gc_disable, real_collect };
  const uint64_t t0 = rd_now_ns();
  const int64_t used0 = a->gc_get_used_size();
  const rd_gc_result r = rd_gcload_collect(&ops, max_generations);
  const uint64_t t1 = rd_now_ns();
  /* how far into the frame: a scene load is hundreds of ms in, a call in play would not be */
  const uint64_t in_frame = g_rd_frame_start_ns && t0 > g_rd_frame_start_ns
                                ? (t0 - g_rd_frame_start_ns) / RD_NS_PER_MS : 0;
  static const char *const what[] = { "", " (GC was off: turned on for it)",
                                       " (GC still disabled by the engine: no-op)" };
  debugPrintf(RD_LOG_TAG_GC " collect asked by the engine %llu ms into the frame: used %lld -> "
              "%lld MB of %lld MB in %llu ms%s\n",
              (unsigned long long)in_frame, MB(used0), MB(a->gc_get_used_size()),
              MB(a->gc_get_heap_size()), (unsigned long long)((t1 - t0) / RD_NS_PER_MS), what[r]);
}

void *rd_gcload_wrap(void *real) {
  real_collect = (void (*)(int))real;
  return (void *)&w_collect;
}
