/* rd_sig.h -- masked AArch64 instruction-pattern search. Host-testable (tests/c/test_rd_sig.c).
 *
 * A pattern is a run of {value, mask} words; an instruction matches if (insn & mask) == value.
 * Masks blank out what changes between builds of the same code (ADRP/ADD/LDR immediates,
 * branch targets) and keep the opcode and registers, which do not. */
#ifndef RD_SIG_H
#define RD_SIG_H

#include <stddef.h>
#include <stdint.h>
#include "rd_a64.h"   /* RD_A64_COND_*, BL encoding */

typedef struct { uint32_t val, mask; } rd_sigw;

#define RD_SIG_X(w)          { (uint32_t)(w), 0xffffffffu }                       /* exact */
#define RD_SIG_BL            { RD_A64_BL_OPCODE, RD_A64_BL_MASK }                         /* bl <any> */
#define RD_SIG_ADRP(rd)      { 0x90000000u | (rd), 0x9f00001fu }                  /* adrp xd, <any> */
#define RD_SIG_ADD(rd, rn)   { 0x91000000u | ((rn) << 5) | (rd), 0xffc003ffu }    /* add xd, xn, #<any> */
#define RD_SIG_LDRW(rt, rn)  { 0xb9400000u | ((rn) << 5) | (rt), 0xffc003ffu }    /* ldr wt, [xn, #<any>] */
#define RD_SIG_LDRX(rt, rn)  { 0xf9400000u | ((rn) << 5) | (rt), 0xffc003ffu }    /* ldr xt, [xn, #<any>] */
#define RD_SIG_BCOND(c)      { 0x54000000u | (c), 0xff00001fu }                   /* b.<c> <any> */
#define RD_SIG_CBZX(rt)      { 0xb4000000u | (rt), 0xff00001fu }
#define RD_SIG_CBNZX(rt)     { 0xb5000000u | (rt), 0xff00001fu }
#define RD_SIG_CBNZW(rt)     { 0x35000000u | (rt), 0xff00001fu }

/* Counts matches of pat[0..plen) in code[0..nwords) (stops counting at 2) and stores the
 * word index of the first match in *first. Returns 0, 1 or 2 (= "2 or more"). */
int rd_sig_scan(const uint32_t *code, size_t nwords, const rd_sigw *pat, size_t plen,
                size_t *first);

#endif
