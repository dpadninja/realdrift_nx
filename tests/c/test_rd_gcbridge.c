/* sources: source/rd_gcbridge.c */
#include <stdint.h>
#include "check.h"
#include <string.h>
#include "rd_gcbridge.h"


/* ---- fake memory for the GC_threads / emulation tests ---- */
#define MEM_BASE 0x10000ull
#define MEM_WORDS 4096
static uint64_t g_mem[MEM_WORDS];
static uint64_t mrd(void *c, uint64_t a) { (void)c; return g_mem[(a - MEM_BASE) / 8]; }
static void mwr(void *c, uint64_t a, uint64_t v) { (void)c; g_mem[(a - MEM_BASE) / 8] = v; }
static const rd_mem MEM = { mrd, mwr, NULL };
#define W(addr) g_mem[((addr) - MEM_BASE) / 8]

static void test_hash_matches_handler(void) {
  /* h = lo32(id>>8) ^ lo32(id); h ^= h>>16; h &= 0xff */
  CHECK(rd_gc_hash(0) == 0);
  CHECK(rd_gc_hash(0x12) == 0x12);
  CHECK(rd_gc_hash(0x1234) == ((0x12u ^ 0x1234u) ^ ((0x12u ^ 0x1234u) >> 16)) % 256);
  uint64_t id = 0x000000045dce8c30ull;   /* a real pthread_t-sized value */
  uint32_t h = (uint32_t)(id >> 8) ^ (uint32_t)id; h ^= h >> 16;
  CHECK(rd_gc_hash(id) == (h & 0xff));
}

static void test_lookup_walks_bucket_chain(void) {
  memset(g_mem, 0, sizeof g_mem);
  const uint64_t table = MEM_BASE, e1 = MEM_BASE + 0x1000, e2 = MEM_BASE + 0x1100;
  const uint64_t id1 = 0x4500, id2 = 0x4500 + 0x10000;   /* different ids, check bucket */
  unsigned b = rd_gc_hash(id2);
  W(table + 8ull * b) = e1;                 /* bucket -> e1 -> e2 */
  W(e1 + RD_GC_THR_ID) = id1; W(e1 + RD_GC_THR_NEXT) = e2;
  W(e2 + RD_GC_THR_ID) = id2; W(e2 + RD_GC_THR_NEXT) = 0;
  CHECK(rd_gc_lookup_thread(&MEM, table, id2) == e2);
  CHECK(rd_gc_lookup_thread(&MEM, table, 0x7777) == 0);
  CHECK(rd_gc_lookup_thread(&MEM, 0, id2) == 0);
}

static void test_emulate_suspend_and_restart(void) {
  memset(g_mem, 0, sizeof g_mem);
  const uint64_t entry = MEM_BASE + 0x100, sp = MEM_BASE + 0x7f08;   /* misaligned on purpose */
  uint64_t regs[RD_GC_NREGS];
  for (int i = 0; i < RD_GC_NREGS; i++) regs[i] = 0xa000 + i;
  CHECK(!rd_gc_already_stopped(&MEM, entry, 6));
  uint64_t blk = rd_gc_emulate_suspend(&MEM, entry, 6, sp, regs);
  CHECK((blk & 0xf) == 0);
  CHECK(blk + 8ull * RD_GC_NREGS + RD_GC_SP_GAP <= sp);       /* below sp, with the gap */
  CHECK(sp - blk < RD_GC_SP_GAP + 8ull * RD_GC_NREGS + 16);
  for (int i = 0; i < RD_GC_NREGS; i++) CHECK(W(blk + 8ull * i) == 0xa000u + (unsigned)i);
  CHECK(W(entry + RD_GC_THR_STACK_PTR) == blk);
  CHECK(W(entry + RD_GC_THR_LAST_STOP) == 6);
  CHECK(rd_gc_already_stopped(&MEM, entry, 6));
  rd_gc_emulate_restart(&MEM, entry, 6);
  CHECK(W(entry + RD_GC_THR_LAST_STOP) == 7);
  CHECK(rd_gc_already_stopped(&MEM, entry, 6));                /* 7 & ~1 == 6 */
  CHECK(!rd_gc_already_stopped(&MEM, entry, 8));               /* next collection */
}

int main(void) {
  RUN(test_hash_matches_handler);
  RUN(test_lookup_walks_bucket_chain);
  RUN(test_emulate_suspend_and_restart);
  return g_failures != 0;
}
