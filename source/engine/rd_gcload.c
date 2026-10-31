/* rd_gcload.c -- see rd_gcload.h: the call sequence. MIT (see LICENSE). */
#include "rd_gcload.h"

rd_gc_result rd_gcload_collect(const rd_gc_ops *ops, int max_generations) {
  const int was_off = ops->is_disabled();
  if (was_off) ops->enable();
  const int still_off = was_off && ops->is_disabled();
  ops->collect(max_generations);
  if (was_off) ops->disable();
  return still_off ? RD_GC_STILL_OFF : was_off ? RD_GC_RAN_TURNED_ON : RD_GC_RAN;
}
