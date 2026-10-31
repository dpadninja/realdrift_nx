/* rd_gcload.c -- see rd_gcload.h: the decision. MIT (see LICENSE). */
#include "rd_gcload.h"
#include "rd_platform.h"

int rd_gcload_due(uint64_t frame_ns, unsigned stops) {
  return frame_ns >= RD_GC_LOAD_FRAME_NS && stops > 0;
}
