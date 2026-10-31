/* rd_gcload_hook.c -- see rd_gcload.h: the il2cpp glue. MIT (see LICENSE). */
#include <stdint.h>
#include "rd_gcload.h"
#include "rd_il2cpp.h"
#include "rd_log.h"
#include "rd_platform.h"
#include "util.h"

#define MB(x) ((long long)((x) >> 20))

void rd_gcload_after_frame(uint64_t frame_ns, unsigned stops) {
  if (!rd_gcload_due(frame_ns, stops)) return;
  const rd_il2cpp_api *a = &g_il2cpp;
  if (!a->gc_collect || !a->gc_enable || !a->gc_disable || !a->gc_is_disabled ||
      !a->gc_get_used_size || !a->gc_get_heap_size)
    return;
  const int64_t used0 = a->gc_get_used_size();
  const uint64_t t0 = rd_now_ns();
  const int was_off = a->gc_is_disabled();
  if (was_off) a->gc_enable();
  const int still_off = a->gc_is_disabled();   /* the engine may hold its own disable */
  a->gc_collect(0);
  if (was_off) a->gc_disable();
  const uint64_t ms = (rd_now_ns() - t0) / RD_NS_PER_MS;
  debugPrintf(RD_LOG_TAG_GC " collected after a load: used %lld -> %lld MB of %lld MB in %llu ms%s\n",
              MB(used0), MB(a->gc_get_used_size()), MB(a->gc_get_heap_size()),
              (unsigned long long)ms, still_off ? " (GC still disabled by the engine: no-op)" : "");
}
