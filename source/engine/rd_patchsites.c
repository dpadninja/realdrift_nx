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

/* ---------------------------------------------------------------- libunity: blit type */
static const rd_sigw BLIT_TYPE[] = {
  RD_SIG_BL,                     /*  0 bl   GetPlayerSettings          */
  RD_SIG_LDRW(8, 0),             /*  1 ldr  w8, [x0, #androidBlitType] */
  RD_SIG_X(0x7100091f),          /*  2 cmp  w8, #0x2 (Auto)            */
  RD_SIG_BCOND(RD_A64_COND_EQ),  /*  3 b.eq                            */
  RD_SIG_X(0x7100051f),          /*  4 cmp  w8, #0x1 (Never)           */
  RD_SIG_BCOND(RD_A64_COND_NE),  /*  5 b.ne                            */
};
static const rd_sigw GET_SETTINGS[] = {
  RD_SIG_X(0xf81f0ffe),          /*  0 str  x30, [sp, #-0x10]!         */
  RD_SIG_X(0x2a1f03e0),          /*  1 mov  w0, wzr  (context 0)       */
  RD_SIG_BL,                     /*  2 bl   GetManagerFromContext      */
  RD_SIG_X(0xf84107fe),          /*  3 ldr  x30, [sp], #0x10           */
  RD_SIG_X(RD_A64_RET),          /*  4 ret                             */
};

/* 1 if the words at link-time address va match pat. */
static int matches_at(const rd_image *im, uint64_t va, const rd_sigw *pat, size_t plen) {
  if (va < im->vaddr || (va - im->vaddr) % 4) return 0;
  const size_t at = (size_t)((va - im->vaddr) / 4);
  if (at + plen > im->nwords) return 0;
  for (size_t k = 0; k < plen; k++)
    if ((im->code[at + k] & pat[k].mask) != pat[k].val) return 0;
  return 1;
}

int rd_find_blit_type(const rd_image *im, uint64_t *get_settings, uint64_t *get_manager,
                      uint32_t *field) {
  size_t i = 0;
  int n = rd_sig_scan(im->code, im->nwords, BLIT_TYPE, NPAT(BLIT_TYPE), &i);
  if (n == 0) return RD_SITE_NOT_FOUND;
  if (n > 1) return RD_SITE_AMBIGUOUS;
  const uint64_t gs = rd_a64_branch_target(va_of(im, i), im->code[i]);
  if (!matches_at(im, gs, GET_SETTINGS, NPAT(GET_SETTINGS))) return RD_SITE_BAD_CHECK;
  const size_t g = (size_t)((gs - im->vaddr) / 4);
  uint32_t off = 0;
  rd_a64_ldr_w_uimm(im->code[i + 1], NULL, &off);
  *get_settings = gs;
  *get_manager = rd_a64_branch_target(gs + 8, im->code[g + 2]);
  *field = off;
  return RD_SITE_OK;
}

/* ---------------------------------------------------------------- libunity: il2cpp copy */
static const rd_sigw IL2CPP_COPY[] = {
  RD_SIG_ADD(0, 31),             /*  0 add  x0, sp, #<source entry>    */
  RD_SIG_ADD(1, 31),             /*  1 add  x1, sp, #<dest entry>      */
  RD_SIG_X(0x320003e2),          /*  2 orr  w2, wzr, #0x1 (replace)    */
  RD_SIG_BL,                     /*  3 bl   CopyDirectory              */
  { 0x37000000u, 0xfff8001fu },  /*  4 tbnz w0, #0, <ok>               */
  RD_SIG_LDRX(8, 31),            /*  5 ldr  x8, [sp, #...]             */
  RD_SIG_LDRX(9, 31),            /*  6 ldr  x9, [sp, #...]             */
  RD_SIG_ADRP(0),                /*  7 adrp x0, "il2cpp failed to extract resources"  */
  RD_SIG_ADD(0, 0),              /*  8 add  x0, x0, #...               */
};
static const char IL2CPP_COPY_MSG[] = "  il2cpp failed to extract resources";
#define COPY_DIR_PROLOGUE 0xa9ba6ffcu   /* stp x28, x27, [sp, #-0x60]! */
#define PAGE_BYTES 0x1000u

int rd_find_il2cpp_copy(const rd_image *im, uint64_t *call, uint64_t *copy, uint64_t *veneer) {
  size_t i = 0;
  int n = rd_sig_scan(im->code, im->nwords, IL2CPP_COPY, NPAT(IL2CPP_COPY), &i);
  if (n == 0) return RD_SITE_NOT_FOUND;
  if (n > 1) return RD_SITE_AMBIGUOUS;
  if (!im->cstr) return RD_SITE_BAD_CHECK;
  const char *s = im->cstr(im->ctx, pair_addr(im, i + 7, i + 8));
  if (!s || strncmp(s, IL2CPP_COPY_MSG, sizeof IL2CPP_COPY_MSG - 1)) return RD_SITE_BAD_CHECK;
  const uint64_t fn = rd_a64_branch_target(va_of(im, i + 3), im->code[i + 3]);
  const size_t f = (size_t)((fn - im->vaddr) / 4);
  if (fn < im->vaddr || f >= im->nwords || im->code[f] != COPY_DIR_PROLOGUE) return RD_SITE_BAD_CHECK;
  const uint64_t end = im->vaddr + 4ull * im->nwords;
  const uint64_t v = (end + 15) & ~15ull;
  if (v + 4ull * RD_A64_ABS_JUMP_WORDS > ((end + PAGE_BYTES - 1) & ~(uint64_t)(PAGE_BYTES - 1)))
    return RD_SITE_BAD_CHECK;                        /* no room left in the last code page */
  *call = va_of(im, i + 3);
  *copy = fn;
  *veneer = v;
  return RD_SITE_OK;
}

/* ---------------------------------------------------------------- libunity: splash flag */
static const rd_sigw SPLASH_NAME[] = {
  RD_SIG_ADRP(1),                /*  0 adrp x1, "m_ShowUnitySplashScreen" */
  RD_SIG_ADD(3, 20),             /*  1 add  x3, x20, #<field>          */
  RD_SIG_ADD(1, 1),              /*  2 add  x1, x1, #...               */
};
static const rd_sigw SPLASH_CALL[] = {
  RD_SIG_ADD(0, 20),             /*  0 add  x0, x20, #<splash settings> */
  RD_SIG_X(0xaa1303e1),          /*  1 mov  x1, x19  (transfer)        */
  RD_SIG_BL,                     /*  2 bl   SplashSettings::Transfer   */
};
static const char SPLASH_FIELD[] = "m_ShowUnitySplashScreen";
#define SPLASH_TRANSFER_MAX 0x100u   /* bytes from the function's start to the name */

/* Index of the next match of pat at or after `from`, or nwords. */
static size_t next_match(const rd_image *im, const rd_sigw *pat, size_t plen, size_t from) {
  size_t k = 0;
  if (from >= im->nwords || !rd_sig_scan(im->code + from, im->nwords - from, pat, plen, &k))
    return im->nwords;
  return from + k;
}

/* imm of `add xd, xn, #imm` (0 if the word is not one). */
static uint32_t add_imm(uint32_t insn) {
  unsigned rd, rn;
  uint32_t imm = 0;
  return rd_a64_add_x_imm(insn, &rd, &rn, &imm) ? imm : 0;
}

int rd_find_splash_flag(const rd_image *im, uint32_t *field) {
  if (!im->cstr) return RD_SITE_BAD_CHECK;
  size_t ref = 0;
  int refs = 0;
  for (size_t i = next_match(im, SPLASH_NAME, NPAT(SPLASH_NAME), 0); i < im->nwords;
       i = next_match(im, SPLASH_NAME, NPAT(SPLASH_NAME), i + 1)) {
    const char *s = im->cstr(im->ctx, pair_addr(im, i, i + 2));
    if (s && !strcmp(s, SPLASH_FIELD)) { ref = i; refs++; }
  }
  if (refs == 0) return RD_SITE_NOT_FOUND;
  if (refs > 1) return RD_SITE_AMBIGUOUS;
  const uint64_t at = va_of(im, ref);
  size_t call = 0;
  int calls = 0;
  for (size_t i = next_match(im, SPLASH_CALL, NPAT(SPLASH_CALL), 0); i < im->nwords;
       i = next_match(im, SPLASH_CALL, NPAT(SPLASH_CALL), i + 1)) {
    const uint64_t t = rd_a64_branch_target(va_of(im, i + 2), im->code[i + 2]);
    if (t > at || at - t >= SPLASH_TRANSFER_MAX) continue;
    int ret = 0;                                     /* the name must be in that function */
    for (uint64_t a = t; a < at && !ret; a += 4) ret = im->code[(a - im->vaddr) / 4] == RD_A64_RET;
    if (!ret) { call = i; calls++; }
  }
  if (calls == 0) return RD_SITE_NOT_FOUND;
  if (calls > 1) return RD_SITE_AMBIGUOUS;
  const uint32_t base = add_imm(im->code[call]), off = add_imm(im->code[ref + 1]);
  if (!base || !off) return RD_SITE_BAD_CHECK;
  *field = base + off;
  return RD_SITE_OK;
}

/* ---------------------------------------------------------------- il2cpp: skip a clear */
static const rd_sigw SET_AND_CLEAR[] = {
  RD_SIG_X(RD_A64_SUB_SP_0X20),  /*  0 sub  sp, sp, #0x20              */
  RD_SIG_X(0xa9017bfd),          /*  1 stp  x29, x30, [sp, #0x10]      */
  RD_SIG_X(0x910043fd),          /*  2 add  x29, sp, #0x10             */
  RD_SIG_X(0xaa1f03e1),          /*  3 mov  x1, xzr  (MethodInfo)      */
  RD_SIG_BL,                     /*  4 bl   RenderTexture.set_active   */
};
static const rd_sigw FRAME_EPILOGUE[] = {
  RD_SIG_X(0xa9417bfd),          /*  0 ldp  x29, x30, [sp, #0x10]      */
  RD_SIG_X(0x910083ff),          /*  1 add  sp, sp, #0x20              */
  RD_SIG_X(RD_A64_RET),          /*  2 ret                             */
};

int rd_find_skip_clear(const uint32_t *code, size_t n, size_t *after_set, size_t *epilogue) {
  size_t i = 0, e = 0;
  if (n < NPAT(SET_AND_CLEAR) || rd_sig_scan(code, NPAT(SET_AND_CLEAR), SET_AND_CLEAR,
                                             NPAT(SET_AND_CLEAR), &i) != 1)
    return RD_SITE_NOT_FOUND;
  const size_t from = NPAT(SET_AND_CLEAR);
  int k = rd_sig_scan(code + from, n - from, FRAME_EPILOGUE, NPAT(FRAME_EPILOGUE), &e);
  if (k == 0) return RD_SITE_NOT_FOUND;
  if (k > 1) return RD_SITE_AMBIGUOUS;
  e += from;
  int calls = 0;                                     /* the clear: at least one call between */
  for (size_t j = from; j < e; j++) calls += rd_a64_is_bl(code[j]);
  if (!calls) return RD_SITE_BAD_CHECK;
  *after_set = from;
  *epilogue = e;
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
