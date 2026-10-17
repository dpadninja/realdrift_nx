/* rd_gcbridge.c -- see rd_gcbridge.h. MIT (see LICENSE). */
#include "rd_gcbridge.h"

unsigned rd_gc_hash(uint64_t id) {
  uint32_t h = (uint32_t)(id >> 8) ^ (uint32_t)id;   /* eor w9, w10, w8 */
  h ^= h >> 16;                                       /* eor w9, w9, w9, lsr #16 */
  return h & (RD_GC_THREAD_TABLE_SZ - 1);             /* and x9, x9, #0xff */
}

uint64_t rd_gc_lookup_thread(const rd_mem *m, uint64_t table, uint64_t id) {
  if (!m || !table) return 0;
  uint64_t p = m->rd(m->ctx, table + 8ull * rd_gc_hash(id));
  for (int guard = 0; p && guard < RD_GC_MAX_CHAIN; guard++) {
    if (m->rd(m->ctx, p + RD_GC_THR_ID) == id) return p;
    p = m->rd(m->ctx, p + RD_GC_THR_NEXT);
  }
  return 0;
}

int rd_gc_already_stopped(const rd_mem *m, uint64_t entry, uint64_t stop_count) {
  return (m->rd(m->ctx, entry + RD_GC_THR_LAST_STOP) & ~1ull) == stop_count;
}

uint64_t rd_gc_emulate_suspend(const rd_mem *m, uint64_t entry, uint64_t stop_count,
                               uint64_t sp, const uint64_t regs[RD_GC_NREGS]) {
  uint64_t blk = (sp - RD_GC_SP_GAP - 8ull * RD_GC_NREGS) & ~0xfull;
  for (int i = 0; i < RD_GC_NREGS; i++) m->wr(m->ctx, blk + 8ull * i, regs[i]);
  m->wr(m->ctx, entry + RD_GC_THR_STACK_PTR, blk);
  m->wr(m->ctx, entry + RD_GC_THR_LAST_STOP, stop_count);
  return blk;
}

void rd_gc_emulate_restart(const rd_mem *m, uint64_t entry, uint64_t stop_count) {
  m->wr(m->ctx, entry + RD_GC_THR_LAST_STOP, stop_count | 1);
}
