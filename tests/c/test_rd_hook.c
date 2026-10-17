/* sources: source/engine/rd_hook.c */
/* Host unit test of rd_hook_relocate: prologues moved into a trampoline keep their meaning.
 * MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_hook.h"

#define PC      0x1000000ull
#define NOP     0xd503201fu
#define LDR_X16 0x58000050u   /* ldr x16, #8 */
#define BR_X16  0xd61f0200u

static uint32_t g_out[RD_HOOK_TRAMP_WORDS];

/* The 64-bit literal at out[i]. */
static uint64_t lit(int i) { return (uint64_t)g_out[i] | ((uint64_t)g_out[i + 1] << 32); }

/* The jump written over the target is ldr x16 / br x16 / the address: 16 bytes, so it
 * replaces exactly RD_HOOK_PATCH_WORDS instructions. */
static void test_jump(void) {
  uint32_t j[RD_HOOK_PATCH_WORDS];
  rd_hook_jump(j, 0x123456789abcull);
  CHECK(j[0] == LDR_X16 && j[1] == BR_X16);
  CHECK(j[2] == 0x56789abcu && j[3] == 0x1234u);
}

/* A typical il2cpp prologue (stp/mov/sub) is copied unchanged and followed by the jump back to
 * target + 16, so calling the trampoline runs the original function. */
static void test_plain_prologue(void) {
  const uint32_t src[4] = { 0xa9bf7bfdu /* stp x29,x30,[sp,#-16]! */, 0x910003fdu /* mov x29,sp */,
                            0xd10083ffu /* sub sp,sp,#0x20 */, 0xaa0003f3u /* mov x19,x0 */ };
  const int n = rd_hook_relocate(src, PC, g_out);
  CHECK(n == 8 && !memcmp(g_out, src, sizeof src));
  CHECK(g_out[4] == LDR_X16 && g_out[5] == BR_X16 && lit(6) == PC + 16);
}

/* bl becomes an absolute call (ldr x16 / blr x16) that returns to the next relocated
 * instruction; b becomes an absolute jump -- the call target must not move with the code. */
static void test_branches(void) {
  const uint32_t src[4] = { 0x94000010u /* bl +0x40 */, NOP, NOP, NOP };
  int n = rd_hook_relocate(src, PC, g_out);
  CHECK(n > 0);
  CHECK(g_out[0] == 0x58000070u /* ldr x16,#12 */ && g_out[1] == 0xd63f0200u /* blr x16 */);
  CHECK(g_out[2] == 0x14000003u /* b #12 */ && lit(3) == PC + 0x40);
  CHECK(g_out[5] == NOP);
  const uint32_t back[4] = { 0x97fffffcu /* bl -0x10 */, NOP, NOP, NOP };
  n = rd_hook_relocate(back, PC, g_out);
  CHECK(n > 0 && lit(3) == PC - 0x10);
  const uint32_t jump_last[4] = { NOP, NOP, NOP, 0x14000004u /* b +0x10 at PC+12 */ };
  n = rd_hook_relocate(jump_last, PC, g_out);
  CHECK(n > 0 && g_out[3] == LDR_X16 && lit(5) == PC + 12 + 0x10);
}

/* Conditional branches keep their test but jump over a skip into an absolute jump to the
 * original target: b.cond, cbz and tbz all keep register, bit and condition. */
static void test_conditional(void) {
  const uint32_t src[4] = { 0x54000101u /* b.ne +0x20 */, 0xb4000060u /* cbz x0,+0xc */,
                            0x37080040u /* tbnz w0,#1,+8 */, NOP };
  const int n = rd_hook_relocate(src, PC, g_out);
  CHECK(n == 6 * 3 + 1 + 4);
  CHECK(g_out[0] == 0x54000041u /* b.ne +8 */ && g_out[1] == 0x14000005u && lit(4) == PC + 0x20);
  CHECK(g_out[6] == 0xb4000040u /* cbz x0,+8 */ && lit(10) == PC + 4 + 0xc);
  CHECK(g_out[12] == 0x37080040u /* tbnz w0,#1,+8 (already 2) */ && lit(16) == PC + 8 + 8);
  CHECK(g_out[18] == NOP);
}

/* adr/adrp load the address they would have computed at the original place, so data and
 * metadata references stay right after the move. */
static void test_adr_adrp(void) {
  const uint32_t src[4] = { 0x90000008u /* adrp x8, +0 pages */, 0xb0000009u /* adrp x9, +1 page */,
                            0x10000101u /* adr x1, +0x20 */, NOP };
  const uint64_t pc = PC + 0x123;   /* adrp works on pages */
  rd_hook_relocate(src, pc, g_out);
  CHECK(g_out[0] == 0x58000048u /* ldr x8,#8 */ && g_out[1] == 0x14000003u);
  CHECK(lit(2) == (pc & ~0xfffull));
  CHECK(g_out[4] == 0x58000049u && lit(6) == ((pc + 4) & ~0xfffull) + 0x1000);
  CHECK(g_out[8] == 0x58000041u && lit(10) == pc + 8 + 0x20);
}

/* Literal loads become a load through the absolute address, with the original width and
 * sign extension. */
static void test_ldr_literal(void) {
  const uint32_t src[4] = { 0x58000080u /* ldr x0, +0x10 */, 0x18000081u /* ldr w1, +0x10 */,
                            0x98000082u /* ldrsw x2, +0x10 */, NOP };
  rd_hook_relocate(src, PC, g_out);
  CHECK(g_out[0] == 0x58000060u /* ldr x0,#12 */ && g_out[1] == 0xf9400000u /* ldr x0,[x0] */);
  CHECK(lit(3) == PC + 0x10);
  CHECK(g_out[6] == 0xb9400021u /* ldr w1,[x1] */ && lit(8) == PC + 4 + 0x10);
  CHECK(g_out[11] == 0xb9800042u /* ldrsw x2,[x2] */ && lit(13) == PC + 8 + 0x10);
}

/* What cannot be moved safely is refused: a function ending within the patch (the jump would
 * overwrite the next function) and SIMD / prefetch literal loads. */
static void test_refusals(void) {
  const uint32_t short_fn[4] = { 0x52800020u /* mov w0,#1 */, 0xd65f03c0u /* ret */, NOP, NOP };
  CHECK(rd_hook_relocate(short_fn, PC, g_out) == -1);
  const uint32_t tail[4] = { 0x14000010u /* b */, NOP, NOP, NOP };
  CHECK(rd_hook_relocate(tail, PC, g_out) == -1);
  const uint32_t br[4] = { NOP, NOP, 0xd61f0200u /* br x16 */, NOP };
  CHECK(rd_hook_relocate(br, PC, g_out) == -1);
  const uint32_t simd[4] = { 0x1c000080u /* ldr s0, +0x10 */, NOP, NOP, NOP };
  CHECK(rd_hook_relocate(simd, PC, g_out) == -1);
  const uint32_t prfm[4] = { 0xd8000080u /* prfm, +0x10 */, NOP, NOP, NOP };
  CHECK(rd_hook_relocate(prfm, PC, g_out) == -1);
}

/* The worst case (four conditional branches) still fits the trampoline. */
static void test_worst_case_fits(void) {
  const uint32_t src[4] = { 0x54000101u, 0x54000101u, 0x54000101u, 0x54000101u };
  const int n = rd_hook_relocate(src, PC, g_out);
  CHECK(n == 4 * 6 + 4 && n <= RD_HOOK_TRAMP_WORDS);
}

int main(void) {
  RUN(test_jump);
  RUN(test_plain_prologue);
  RUN(test_branches);
  RUN(test_conditional);
  RUN(test_adr_adrp);
  RUN(test_ldr_literal);
  RUN(test_refusals);
  RUN(test_worst_case_fits);
  return g_failures != 0;
}
