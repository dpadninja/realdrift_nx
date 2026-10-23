/* rd_patchsites.h -- find the loader's patch sites by instruction patterns instead of fixed
 * per-build offsets. Host-testable; also built as the CLI tools/find_patch_sites.
 *
 * Every finder requires its pattern to match EXACTLY ONCE and cross-checks the result
 * (string contents, branch targets, the call between two functions), so a different binary
 * yields "not found" instead of a wrong address. Verified on Real Drift 5.0.7 and 5.0.8
 * (tests/test_patch_sites.py).
 * MIT (see LICENSE). */
#ifndef RD_PATCHSITES_H
#define RD_PATCHSITES_H

#include <stddef.h>
#include <stdint.h>
#include "rd_gcbridge.h"

typedef struct {
  const uint32_t *code;    /* the executable segment's words */
  uint64_t        vaddr;   /* link-time address of code[0] */
  size_t          nwords;
  /* NUL-terminated string at link-time address `vaddr`, or NULL (for cross-checks). */
  const char   *(*cstr)(void *ctx, uint64_t vaddr);
  void           *ctx;
} rd_image;

/* Result codes */
#define RD_SITE_OK         1
#define RD_SITE_NOT_FOUND  0
#define RD_SITE_AMBIGUOUS -1
#define RD_SITE_BAD_CHECK -2

/* libunity: the API-level gate that enables Java-Choreographer VSYNC timing
 * (`bl GetApiLevel; cmp w0,#16; b.lt <ret>`, then "Choreographer available: Enabling VSYNC
 * timing"). *gate = address of the cmp, *skip = target of the b.lt (the epilogue). */
int rd_find_choreo_gate(const rd_image *im, uint64_t *gate, uint64_t *skip);

/* libunity: the Android back-buffer choice, from the code that reads PlayerSettings'
 * androidBlitType (0 Always, 1 Never, 2 Auto): `bl GetPlayerSettings; ldr w8,[x0,#field];
 * cmp w8,#2; b.eq; cmp w8,#1; b.ne`. Cross-check: GetPlayerSettings is `str x30,[sp,#-16]!;
 * mov w0,wzr; bl GetManagerFromContext; ldr x30,[sp],#16; ret`. Outputs the two functions'
 * link-time addresses and the field's offset in the PlayerSettings object. */
int rd_find_blit_type(const rd_image *im, uint64_t *get_settings, uint64_t *get_manager,
                      uint32_t *field);

/* libunity: the per-launch copy of the il2cpp data, `add x0,sp,#src; add x1,sp,#dst;
 * orr w2,wzr,#1; bl CopyDirectory; tbnz w0,#0,<ok>`, then the "il2cpp failed to extract
 * resources" message; CopyDirectory starts `stp x28,x27,[sp,#-0x60]!`. *call = the bl,
 * *copy = CopyDirectory, *veneer = 16 bytes of padding after the code segment, in its last
 * page (for an absolute jump the bl can reach). */
int rd_find_il2cpp_copy(const rd_image *im, uint64_t *call, uint64_t *copy, uint64_t *veneer);

/* libunity: the offset of m_ShowUnitySplashScreen in the PlayerSettings object. The splash
 * settings' Transfer names it (`adrp x1; add x3,x20,#f; add x1,"m_ShowUnitySplashScreen"`),
 * and PlayerSettings' Transfer calls that function as `add x0,x20,#base; mov x1,x19; bl`:
 * *field = base + f. Each must be the only one. */
int rd_find_splash_flag(const rd_image *im, uint32_t *field);

/* An il2cpp method shaped like SleekRenderPostProcess.SetActiveRenderTextureAndClear: a
 * 0x20-byte frame, `bl` (RenderTexture.active = target), then a GL.Clear, then the epilogue
 * `ldp x29,x30,[sp,#0x10]; add sp,sp,#0x20; ret` within n words. *after_set = index of the word
 * after the first bl, *epilogue = index of the ldp: a `b` from one to the other skips the clear.
 * RD_SITE_* result. */
int rd_find_skip_clear(const uint32_t *code, size_t n, size_t *after_set, size_t *epilogue);

/* libil2cpp: Boehm GC stop-the-world globals, from GC_suspend_handler_inner, the suspend
 * signal wrapper that calls it, and the restart signal handler. */
int rd_find_gc_vars(const rd_image *im, rd_gc_vars *out);

#endif
