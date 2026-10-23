/* rd_a64.c -- see rd_a64.h. MIT (see LICENSE). */
#include "rd_a64.h"

/* B / BL with opcode `op`: the 26-bit word offset from `from` to `to`, or 0. */
static uint32_t branch26(uint32_t op, uint64_t from, uint64_t to) {
  int64_t d = (int64_t)(to - from);
  if ((from | to) & 3) return 0;
  if (d < -(1LL << 27) || d >= (1LL << 27)) return 0;
  return op | ((uint32_t)(d >> 2) & 0x03ffffffu);
}

uint32_t rd_a64_b(uint64_t from, uint64_t to) { return branch26(0x14000000u, from, to); }

uint32_t rd_a64_bl(uint64_t from, uint64_t to) { return branch26(RD_A64_BL_OPCODE, from, to); }

void rd_a64_abs_jump(uint32_t out[RD_A64_ABS_JUMP_WORDS], uint64_t to) {
  out[0] = RD_A64_LDR_X16_8;
  out[1] = RD_A64_BR_X16;
  out[2] = (uint32_t)to;          /* little-endian: low word first */
  out[3] = (uint32_t)(to >> 32);
}

/* Sign-extends the low `bits` bits of v (branch offsets are signed immediates). */
static int64_t sext(uint64_t v, int bits) {
  return (int64_t)(v << (64 - bits)) >> (64 - bits);
}

uint64_t rd_a64_branch_target(uint64_t from, uint32_t insn) {
  if ((insn & 0x7c000000u) == 0x14000000u)                 /* B and BL */
    return from + (uint64_t)(sext(insn & 0x03ffffffu, 26) * 4);
  if ((insn & 0xff000010u) == 0x54000000u)
    return from + (uint64_t)(sext((insn >> 5) & 0x7ffffu, 19) * 4);
  return 0;
}

uint64_t rd_a64_adrp_target(uint64_t pc, uint32_t insn, unsigned *rd) {
  if ((insn & 0x9f000000u) != 0x90000000u) return 0;
  uint64_t imm = ((uint64_t)((insn >> 5) & 0x7ffffu) << 2) | ((insn >> 29) & 3u);
  if (rd) *rd = insn & 0x1fu;
  return (pc & ~0xfffull) + (uint64_t)(sext(imm, 21) * 4096);
}

int rd_a64_ldr_w_uimm(uint32_t insn, unsigned *rn, uint32_t *off) {
  if ((insn & 0xffc00000u) != 0xb9400000u) return 0;
  if (rn) *rn = (insn >> 5) & 0x1fu;
  if (off) *off = ((insn >> 10) & 0xfffu) * 4u;
  return 1;
}

int rd_a64_add_x_imm(uint32_t insn, unsigned *rd, unsigned *rn, uint32_t *imm) {
  if ((insn & 0xff800000u) != 0x91000000u) return 0;
  if (rd) *rd = insn & 0x1fu;
  if (rn) *rn = (insn >> 5) & 0x1fu;
  if (imm) *imm = ((insn >> 10) & 0xfffu) << ((insn & (1u << 22)) ? 12 : 0);
  return 1;
}

