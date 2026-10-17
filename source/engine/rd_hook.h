/* rd_hook.h -- inline hooks: redirect a native function (any code in libil2cpp / libunity) to
 * ours, with a trampoline that runs the original.
 *
 * The first RD_HOOK_PATCH_WORDS instructions of the target are replaced by an absolute jump
 * (ldr x16, #8; br x16; .quad fn). They are copied into a trampoline first; instructions that
 * depend on their address (b, bl, b.cond, cbz/cbnz, tbz/tbnz, adr, adrp, ldr literal) are
 * rewritten to absolute forms, then the trampoline jumps back to target + 16. Calling the
 * trampoline therefore runs the original function.
 *
 * Refused (nothing is changed): a function that ends (ret / br / b) within its first three
 * instructions -- patching would overwrite the next function -- and prologues with PC-relative
 * forms not listed above (SIMD/prefetch literal loads). x16 is the AAPCS intra-call scratch
 * register, so the patch clobbers nothing the function relies on at entry.
 *
 * rd_hook_relocate is pure (host-tested, tests/c/test_rd_hook.c); rd_hook_install
 * (rd_hook_install.c) maps the trampoline memory and patches the code.
 * MIT (see LICENSE). */
#ifndef RD_HOOK_H
#define RD_HOOK_H

#include <stdint.h>

#define RD_HOOK_PATCH_WORDS 4     /* instructions replaced at the target (16 bytes) */
#define RD_HOOK_TRAMP_WORDS 32    /* room for 4 relocated instructions + the jump back */

/* Writes the jump to `dest` (RD_HOOK_PATCH_WORDS words) into out. */
void rd_hook_jump(uint32_t out[RD_HOOK_PATCH_WORDS], uint64_t dest);

/* Builds the trampoline for the function whose first RD_HOOK_PATCH_WORDS instructions are
 * src (at address src_pc) into out (RD_HOOK_TRAMP_WORDS words). Every rewritten form is
 * absolute, so the trampoline may live anywhere. Returns the number of words written, or -1
 * if the prologue cannot be moved (see the header comment). */
int rd_hook_relocate(const uint32_t src[RD_HOOK_PATCH_WORDS], uint64_t src_pc, uint32_t *out);

/* Hooks the function at `target`: from now on it runs `fn`; *orig (not NULL) receives the
 * trampoline that runs the original. Returns 0, -1 if the prologue cannot be moved, -2 if no
 * trampoline memory is left or the code cannot be patched. Main thread; a thread running the
 * function's first instructions at this moment may crash, so hook early (before the code is
 * busy) when you can. */
int rd_hook_install(void *target, void *fn, void **orig);

#endif
