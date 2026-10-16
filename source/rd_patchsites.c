/* rd_patchsites.c -- see rd_patchsites.h. MIT (see LICENSE).
 *
 * Patterns are taken from llvm-objdump of libunity.so / libil2cpp.so of Real Drift 5.0.7 and
 * 5.0.8 (Unity 2018.4.14f1): every word that differs between the two builds is masked. */
#include <string.h>
#include "rd_a64.h"
#include "rd_patchsites.h"
#include "rd_sig.h"

#define NPAT(p) (sizeof(p) / sizeof((p)[0]))

static uint64_t va_of(const rd_image *im, size_t idx) { return im->vaddr + 4ull * idx; }

/* adrp xA, page ; (add|ldr) ..., [xA, #imm]  ->  page + imm */
static uint64_t pair_addr(const rd_image *im, size_t adrp_idx, size_t use_idx) {
  unsigned ra, rd, rn; uint32_t imm;
  uint64_t page = rd_a64_adrp_target(va_of(im, adrp_idx), im->code[adrp_idx], &ra);
  if (!page) return 0;
  uint32_t use = im->code[use_idx];
  if (rd_a64_add_x_imm(use, &rd, &rn, &imm) && rn == ra) return page + imm;
  if (rd_a64_ldr_w_uimm(use, &rn, &imm) && rn == ra) return page + imm;
  if ((use & 0xffc00000u) == 0xf9400000u && ((use >> 5) & 0x1fu) == ra)      /* ldr xt, [xn, #imm] */
    return page + 8ull * ((use >> 10) & 0xfffu);
  return 0;
}

/* ---------------------------------------------------------------- libunity: Choreographer */
static const rd_sigw CHOREO[] = {
  RD_SIG_X(RD_A64_SUB_SP_0X20),  /*  0 sub  sp, sp, #0x20              */
  RD_SIG_X(0xa9017bf3),          /*  1 stp  x19, x30, [sp, #0x10]      */
  RD_SIG_BL,                     /*  2 bl   GetApiLevel                */
  RD_SIG_X(0x7100401f),          /*  3 cmp  w0, #0x10       <- gate    */
  RD_SIG_BCOND(RD_A64_COND_LT),      /*  4 b.lt <epilogue>                 */
  RD_SIG_X(0x910023e0),          /*  5 add  x0, sp, #0x8               */
  RD_SIG_X(0x910023f3),          /*  6 add  x19, sp, #0x8              */
  RD_SIG_BL,                     /*  7                                 */
  RD_SIG_X(0xb2400260),          /*  8 orr  x0, x19, #0x1              */
  RD_SIG_X(0x321a03e1),          /*  9 orr  w1, wzr, #0x40             */
  RD_SIG_BL,                     /* 10                                 */
  RD_SIG_BL,                     /* 11                                 */
  RD_SIG_ADRP(0),                /* 12 adrp x0, "Choreographer available..." */
  RD_SIG_ADD(0, 0),              /* 13 add  x0, x0, #...               */
};
static const char CHOREO_MSG[] = "Choreographer available: Enabling VSYNC timing";

int rd_find_choreo_gate(const rd_image *im, uint64_t *gate, uint64_t *skip) {
  size_t i = 0;
  int n = rd_sig_scan(im->code, im->nwords, CHOREO, NPAT(CHOREO), &i);
  if (n == 0) return RD_SITE_NOT_FOUND;
  if (n > 1) return RD_SITE_AMBIGUOUS;
  if (im->cstr) {                                      /* the logged message must be the one */
    const char *s = im->cstr(im->ctx, pair_addr(im, i + 12, i + 13));
    if (!s || strncmp(s, CHOREO_MSG, sizeof CHOREO_MSG - 1)) return RD_SITE_BAD_CHECK;
  }
  uint64_t target = rd_a64_branch_target(va_of(im, i + 4), im->code[i + 4]);
  size_t tidx = (size_t)((target - im->vaddr) / 4);
  if (target < im->vaddr || tidx >= im->nwords || im->code[tidx] != RD_A64_LDP_X19_X30)
    return RD_SITE_BAD_CHECK;
  *gate = va_of(im, i + 3);
  *skip = target;
  return RD_SITE_OK;
}

/* ---------------------------------------------------------------- libil2cpp: Boehm GC */
/* GC_suspend_handler_inner, from `bl pthread_self` to `bl sem_post`. */
static const rd_sigw GC_INNER[] = {
  RD_SIG_BL,                     /*  0 bl   pthread_self               */
  RD_SIG_X(0xaa0003e8),          /*  1 mov  x8, x0                     */
  RD_SIG_ADRP(9),                /*  2 adrp x9, GC_stop_count          */
  RD_SIG_ADD(9, 9),              /*  3 add  x9, x9, #...               */
  RD_SIG_X(0xd348fd0a),          /*  4 lsr  x10, x8, #8                */
  RD_SIG_X(0xc8dffd34),          /*  5 ldar x20, [x9]                  */
  RD_SIG_X(0x4a080149),          /*  6 eor  w9, w10, w8                */
  RD_SIG_X(0x4a494129),          /*  7 eor  w9, w9, w9, lsr #16        */
  RD_SIG_ADRP(10),               /*  8 adrp x10, GC_threads            */
  RD_SIG_X(0x92401d29),          /*  9 and  x9, x9, #0xff              */
  RD_SIG_ADD(10, 10),            /* 10 add  x10, x10, #...             */
  RD_SIG_X(0xf8697955),          /* 11 ldr  x21, [x10, x9, lsl #3]     */
  RD_SIG_CBZX(21),               /* 12 cbz  x21                        */
  RD_SIG_X(0xf94006a9),          /* 13 ldr  x9, [x21, #0x8]   (id)     */
  RD_SIG_X(0xeb08013f),          /* 14 cmp  x9, x8                     */
  RD_SIG_BCOND(RD_A64_COND_EQ),      /* 15 b.eq                            */
  RD_SIG_X(0xf94002b5),          /* 16 ldr  x21, [x21]        (next)   */
  RD_SIG_CBNZX(21),              /* 17 cbnz x21                        */
  RD_SIG_X(0xf9400aa9),          /* 18 ldr  x9, [x21, #0x10]  (last_stop_count) */
  RD_SIG_X(0x927ff929),          /* 19 and  x9, x9, #~1                */
  RD_SIG_X(0xeb14013f),          /* 20 cmp  x9, x20                    */
  RD_SIG_BCOND(RD_A64_COND_NE),      /* 21 b.ne                            */
  RD_SIG_ADRP(9),                /* 22 adrp x9, GC_retry_signals       */
  RD_SIG_LDRW(9, 9),             /* 23 ldr  w9, [x9, #...]             */
  RD_SIG_CBNZW(9),               /* 24 cbnz w9                         */
  RD_SIG_ADRP(9),                /* 25 adrp x9, <log fn ptr>           */
  RD_SIG_LDRX(2, 9),             /* 26 ldr  x2, [x9, #...]             */
  RD_SIG_X(0xa9437bfd),          /* 27 ldp  x29, x30, [sp, #0x30]      */
  RD_SIG_X(0xa9424ff4),          /* 28 ldp  x20, x19, [sp, #0x20]      */
  RD_SIG_X(0xa94157f6),          /* 29 ldp  x22, x21, [sp, #0x10]      */
  RD_SIG_ADRP(0),                /* 30 adrp x0, <message>              */
  RD_SIG_ADD(0, 0),              /* 31                                 */
  RD_SIG_X(0xaa0803e1),          /* 32 mov  x1, x8                     */
  RD_SIG_X(0xf84407f7),          /* 33 ldr  x23, [sp], #0x40           */
  RD_SIG_X(0xd61f0040),          /* 34 br   x2                         */
  RD_SIG_X(0xf90007fd),          /* 35 str  x29, [sp, #0x8]            */
  RD_SIG_X(0xf94007e8),          /* 36 ldr  x8, [sp, #0x8]             */
  RD_SIG_ADRP(0),                /* 37 adrp x0, GC_suspend_ack_sem     */
  RD_SIG_ADD(0, 0),              /* 38 add  x0, x0, #...               */
  RD_SIG_X(0x910042b3),          /* 39 add  x19, x21, #0x10            */
  RD_SIG_X(0xf9000ea8),          /* 40 str  x8, [x21, #0x18]  (stack_ptr) */
  RD_SIG_BL,                     /* 41 bl   sem_post                   */
};
#define GC_INNER_PROLOGUE_WORDS 5   /* str x23,[sp,#-0x40]! ... add x29 precede word 0 */

/* The suspend-signal wrapper: checks the signal number, then calls GC_suspend_handler_inner. */
static const rd_sigw GC_SUSPEND_WRAPPER[] = {
  RD_SIG_X(0x2a0003f3),          /*  0 mov  w19, w0                    */
  RD_SIG_BL,                     /*  1 bl   __errno                    */
  RD_SIG_ADRP(8),                /*  2 adrp x8, GC_sig_suspend         */
  RD_SIG_LDRW(8, 8),             /*  3 ldr  w8, [x8, #...]             */
  RD_SIG_X(0x6b13011f),          /*  4 cmp  w8, w19                    */
  RD_SIG_X(0xaa0003f3),          /*  5 mov  x19, x0                    */
  RD_SIG_BCOND(RD_A64_COND_NE),      /*  6 b.ne                            */
  RD_SIG_X(0xb9400274),          /*  7 ldr  w20, [x19]                 */
  RD_SIG_BL,                     /*  8 bl   GC_suspend_handler_inner   */
};

/* The restart-signal handler: returns at once if the signal is GC_sig_thr_restart. */
static const rd_sigw GC_RESTART[] = {
  RD_SIG_ADRP(8),                /*  0 adrp x8, GC_sig_thr_restart     */
  RD_SIG_LDRW(8, 8),             /*  1 ldr  w8, [x8, #...]             */
  RD_SIG_X(0x6b00011f),          /*  2 cmp  w8, w0                     */
  RD_SIG_BCOND(RD_A64_COND_NE),      /*  3 b.ne                            */
  RD_SIG_X(RD_A64_RET),          /*  4 ret                             */
};
#define GC_RESTART_WINDOW_WORDS 0x200   /* searched right after the inner handler */

int rd_find_gc_vars(const rd_image *im, rd_gc_vars *out) {
  size_t i = 0, w = 0, r = 0;
  int n = rd_sig_scan(im->code, im->nwords, GC_INNER, NPAT(GC_INNER), &i);
  if (n == 0) return RD_SITE_NOT_FOUND;
  if (n > 1) return RD_SITE_AMBIGUOUS;
  if (i < GC_INNER_PROLOGUE_WORDS) return RD_SITE_BAD_CHECK;
  const uint64_t inner = va_of(im, i - GC_INNER_PROLOGUE_WORDS);

  n = rd_sig_scan(im->code, im->nwords, GC_SUSPEND_WRAPPER, NPAT(GC_SUSPEND_WRAPPER), &w);
  if (n == 0) return RD_SITE_NOT_FOUND;
  if (n > 1) return RD_SITE_AMBIGUOUS;
  if (rd_a64_branch_target(va_of(im, w + 8), im->code[w + 8]) != inner)
    return RD_SITE_BAD_CHECK;               /* the wrapper must call THIS inner handler */

  size_t from = i, len = im->nwords - from;
  if (len > GC_RESTART_WINDOW_WORDS) len = GC_RESTART_WINDOW_WORDS;
  n = rd_sig_scan(im->code + from, len, GC_RESTART, NPAT(GC_RESTART), &r);
  if (n == 0) return RD_SITE_NOT_FOUND;
  if (n > 1) return RD_SITE_AMBIGUOUS;
  r += from;

  rd_gc_vars v = {
    .sig_suspend     = pair_addr(im, w + 2, w + 3),
    .sig_restart     = pair_addr(im, r + 0, r + 1),
    .retry_signals   = pair_addr(im, i + 22, i + 23),
    .suspend_ack_sem = pair_addr(im, i + 37, i + 38),
    .stop_count      = pair_addr(im, i + 2, i + 3),
    .threads         = pair_addr(im, i + 8, i + 10),
  };
  if (!v.sig_suspend || !v.sig_restart || !v.retry_signals || !v.suspend_ack_sem ||
      !v.stop_count || !v.threads || v.sig_suspend == v.sig_restart)
    return RD_SITE_BAD_CHECK;
  *out = v;
  return RD_SITE_OK;
}
