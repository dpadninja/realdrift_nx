/* rd_hook_install.c -- rd_hook_install (rd_hook.h): trampoline memory and the code patch.
 *
 * Trampolines live in one code region mapped like a loaded library (svcMapProcessCodeMemory,
 * then RX); both the trampolines and the target's first 16 bytes are written through
 * so_patch_code, which maps a writable alias and flushes the caches.
 * MIT (see LICENSE). */
#include <malloc.h>
#include <string.h>
#include <switch.h>
#include "rd_hook.h"
#include "so_util.h"   /* so_patch_code */
#include "util.h"      /* debugPrintf */

#define POOL_BYTES  0x8000                                /* 256 trampolines of 128 bytes */
#define TRAMP_BYTES (RD_HOOK_TRAMP_WORDS * 4)

static uint8_t *g_pool;
static size_t   g_used;

/* Maps the trampoline region once; 0, or -1 (logged). */
static int pool_init(void) {
  if (g_pool) return 0;
  void *src = memalign(0x1000, POOL_BYTES);
  if (!src) return -1;
  memset(src, 0, POOL_BYTES);
  virtmemLock();
  void *dst = virtmemFindCodeMemory(POOL_BYTES, 0x1000);
  VirtmemReservation *rv = dst ? virtmemAddReservation(dst, POOL_BYTES) : NULL;
  virtmemUnlock();
  Result rc = dst ? svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)dst, (u64)src, POOL_BYTES)
                  : MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
  if (R_SUCCEEDED(rc))
    rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)dst, POOL_BYTES, Perm_Rx);
  if (R_FAILED(rc)) {
    debugPrintf("[hook] trampoline memory: %08x\n", rc);
    if (rv) { virtmemLock(); virtmemRemoveReservation(rv); virtmemUnlock(); }
    return -1;
  }
  g_pool = dst;   /* src now belongs to the code mapping; never freed */
  return 0;
}

int rd_hook_install(void *target, void *fn, void **orig) {
  if (!target || !fn) return -1;
  uint32_t head[RD_HOOK_PATCH_WORDS], tramp[RD_HOOK_TRAMP_WORDS];
  memcpy(head, target, sizeof head);
  const int n = rd_hook_relocate(head, (uint64_t)(uintptr_t)target, tramp);
  if (n < 0) {
    debugPrintf("[hook] %p: prologue cannot be moved (tiny or tail-calling function)\n", target);
    return -1;
  }
  if (pool_init() || g_used + TRAMP_BYTES > POOL_BYTES) return -2;
  uint8_t *t = g_pool + g_used;
  if (so_patch_code(t, tramp, (size_t)n * 4)) return -2;
  g_used += TRAMP_BYTES;
  if (orig) *orig = t;   /* before the jump: fn may run (and call orig) as soon as it is in */
  uint32_t jump[RD_HOOK_PATCH_WORDS];
  rd_hook_jump(jump, (uint64_t)(uintptr_t)fn);
  if (so_patch_code(target, jump, sizeof jump)) { if (orig) *orig = NULL; return -2; }
  debugPrintf("[hook] %p -> %p (trampoline %p)\n", target, fn, (void *)t);
  return 0;
}
