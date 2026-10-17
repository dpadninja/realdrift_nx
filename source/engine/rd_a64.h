/* rd_a64.h -- AArch64 encode/decode helpers for the pattern-found code patches.
 * Host-testable (tests/c/test_rd_a64.c).
 * MIT (see LICENSE). */
#ifndef RD_A64_H
#define RD_A64_H

#include <stdint.h>

/* Unconditional `b to` placed at `from`. Returns 0 if misaligned or out of +-128MB range
 * (0 is never a valid B encoding, so callers can treat it as an error). */
uint32_t rd_a64_b(uint64_t from, uint64_t to);

/* Target of the B / BL / B.cond instruction `insn` located at `from`; 0 if it is none. */
uint64_t rd_a64_branch_target(uint64_t from, uint32_t insn);

/* ADRP: page address computed for `insn` at `pc`, and its destination register.
 * Returns 0 if `insn` is not ADRP. */
uint64_t rd_a64_adrp_target(uint64_t pc, uint32_t insn, unsigned *rd);

/* LDR Wt, [Xn, #imm] (unsigned offset): byte offset and base register. 0 if not that form. */
int rd_a64_ldr_w_uimm(uint32_t insn, unsigned *rn, uint32_t *off);

/* ADD Xd, Xn, #imm{, lsl #12}: immediate and registers. 0 if not that form. */
int rd_a64_add_x_imm(uint32_t insn, unsigned *rd, unsigned *rn, uint32_t *imm);

/* ---- encodings the patches and patterns use ----------------------------------------- */
#define RD_A64_COND_EQ      0x0u
#define RD_A64_COND_NE      0x1u
#define RD_A64_COND_LT      0xbu

#define RD_A64_RET          0xd65f03c0u   /* ret                         */
#define RD_A64_LDP_X19_X30  0xa9417bf3u   /* ldp x19, x30, [sp, #0x10]   */
#define RD_A64_SUB_SP_0X20  0xd10083ffu   /* sub sp, sp, #0x20           */
#define RD_A64_BL_OPCODE    0x94000000u   /* bl <imm26>: (insn & MASK) == OPCODE */
#define RD_A64_BL_MASK      0xfc000000u

/* 1 if insn is a BL (branch with link). */
static inline int rd_a64_is_bl(uint32_t insn) {
  return (insn & RD_A64_BL_MASK) == RD_A64_BL_OPCODE;
}

#endif
