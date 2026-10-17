/* rd_hook.c -- see rd_hook.h: the pure prologue relocation. MIT (see LICENSE). */
#include "rd_hook.h"

/* Encodings used in the generated code (x16 = IP0, the AAPCS scratch register). */
#define LDR_X16_LIT(words) (0x58000010u | ((uint32_t)(words) << 5))   /* ldr x16, #4*words */
#define LDR_X_LIT(rt, words) (0x58000000u | ((uint32_t)(words) << 5) | (rt))
#define BR_X16  0xd61f0200u
#define BLR_X16 0xd63f0200u
#define B_FWD(words) (0x14000000u | (uint32_t)(words))                /* b #4*words */

/* Sign-extends the low `bits` bits of v. */
static int64_t sext(uint64_t v, int bits) {
  const uint64_t m = 1ull << (bits - 1);
  v &= (1ull << bits) - 1;
  return (int64_t)((v ^ m) - m);
}

/* Appends a 64-bit literal (two words). */
static int put64(uint32_t *out, int n, uint64_t v) {
  out[n] = (uint32_t)v;
  out[n + 1] = (uint32_t)(v >> 32);
  return n + 2;
}

void rd_hook_jump(uint32_t out[RD_HOOK_PATCH_WORDS], uint64_t dest) {
  out[0] = LDR_X16_LIT(2);
  out[1] = BR_X16;
  put64(out, 2, dest);
}

/* 1 if w never falls through to the next instruction (ret, br, b). */
static int ends_function(uint32_t w) {
  return (w & 0xfffffc1fu) == 0xd65f0000u || (w & 0xfffffc1fu) == 0xd61f0000u ||
         (w & 0xfc000000u) == 0x14000000u;
}

/* Appends the relocated form of w (at pc) to out at n; the new n, or -1 if w cannot move. */
static int relocate_one(uint32_t w, uint64_t pc, uint32_t *out, int n) {
  if ((w & 0x7c000000u) == 0x14000000u) {                       /* b / bl imm26 */
    const uint64_t t = pc + (uint64_t)(sext(w, 26) * 4);
    if (w & 0x80000000u) {                                        /* bl */
      out[n++] = LDR_X16_LIT(3);
      out[n++] = BLR_X16;
      out[n++] = B_FWD(3);
    } else {
      out[n++] = LDR_X16_LIT(2);
      out[n++] = BR_X16;
    }
    return put64(out, n, t);
  }
  int cond = 0;
  uint64_t t = 0;
  uint32_t kept = 0;
  if ((w & 0xff000010u) == 0x54000000u) {                       /* b.cond imm19 */
    cond = 1; t = pc + (uint64_t)(sext(w >> 5, 19) * 4); kept = (w & ~(0x7ffffu << 5)) | (2u << 5);
  } else if ((w & 0x7e000000u) == 0x34000000u) {                /* cbz / cbnz imm19 */
    cond = 1; t = pc + (uint64_t)(sext(w >> 5, 19) * 4); kept = (w & ~(0x7ffffu << 5)) | (2u << 5);
  } else if ((w & 0x7e000000u) == 0x36000000u) {                /* tbz / tbnz imm14 */
    cond = 1; t = pc + (uint64_t)(sext(w >> 5, 14) * 4); kept = (w & ~(0x3fffu << 5)) | (2u << 5);
  }
  if (cond) {   /* the test jumps over "b skip" into an absolute jump to t */
    out[n++] = kept;
    out[n++] = B_FWD(5);
    out[n++] = LDR_X16_LIT(2);
    out[n++] = BR_X16;
    return put64(out, n, t);
  }
  if ((w & 0x1f000000u) == 0x10000000u) {                       /* adr / adrp */
    const uint32_t rd = w & 31;
    const int64_t imm = sext(((uint64_t)(w >> 5) & 0x7ffff) << 2 | ((w >> 29) & 3), 21);
    const uint64_t v = (w & 0x80000000u) ? (pc & ~0xfffull) + (uint64_t)(imm * 4096)
                                         : pc + (uint64_t)imm;
    out[n++] = LDR_X_LIT(rd, 2);
    out[n++] = B_FWD(3);
    return put64(out, n, v);
  }
  if ((w & 0x3b000000u) == 0x18000000u) {                       /* ldr (literal) */
    const uint32_t opc = w >> 30, rt = w & 31;
    if (w & (1u << 26) || opc == 3) return -1;                  /* SIMD / prfm literal */
    static const uint32_t LOAD[3] = { 0xb9400000u, 0xf9400000u, 0xb9800000u };   /* w, x, sw */
    out[n++] = LDR_X_LIT(rt, 3);                                  /* xt = the address */
    out[n++] = LOAD[opc] | (rt << 5) | rt;                        /* load through it */
    out[n++] = B_FWD(3);
    return put64(out, n, pc + (uint64_t)(sext(w >> 5, 19) * 4));
  }
  out[n++] = w;                                                   /* position-independent */
  return n;
}

int rd_hook_relocate(const uint32_t src[RD_HOOK_PATCH_WORDS], uint64_t src_pc, uint32_t *out) {
  for (int i = 0; i < RD_HOOK_PATCH_WORDS - 1; i++)
    if (ends_function(src[i])) return -1;
  int n = 0;
  for (int i = 0; i < RD_HOOK_PATCH_WORDS; i++) {
    n = relocate_one(src[i], src_pc + 4u * (uint64_t)i, out, n);
    if (n < 0) return -1;
  }
  out[n++] = LDR_X16_LIT(2);                                      /* back to the original */
  out[n++] = BR_X16;
  return put64(out, n, src_pc + 4u * RD_HOOK_PATCH_WORDS);
}
