/* shim_dl.c -- dlopen/dlsym over the modules that are already loaded (there is no real
 * dynamic loading); also where il2cpp_add_internal_call is swapped for the icall override
 * wrapper and il2cpp_gc_collect for the one that runs with the GC on (rd_gcload.h).
 *
 * Split out of libc_shim.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>

#include "util.h"
#include "imports.h"
#include "libc_shim.h"
#include "unity_input_hook.h"   /* nx_input_icall_table */
#include "rd_smoke.h"          /* rd_smoke_icall_table */
#include "rd_menu.h"           /* rd_menu_icall_table */
#include "egl.h"
#include "rd_gcload.h"          /* rd_gcload_wrap */


// ---------------------------------------------------------------------------
// dlopen/dlsym over the already-loaded modules (no real dynamic loading).
// dlsym lets the engine look up its own exports / our shims.
// ---------------------------------------------------------------------------

void *dlopen_fake(const char *name, int flags) {
  (void)flags; debugPrintf("dlopen(%s)\n", name ? name : "(self)");
  return (void *)0x1;
}
int dlclose_fake(void *h) { (void)h; return 0; }
const char *dlerror_fake(void) { return NULL; }
void *dlsym_fake(void *handle, const char *symbol) {
  (void)handle;
  if (!symbol) return NULL;
  /* 1) a real export from a loaded module (il2cpp/unity/main) */
  void *p = so_resolve_external(symbol);
  /* Libunity registers engine icalls through il2cpp_add_internal_call; hand it
   * our wrapper so UnityEngine.Input icalls are replaced and the particle Emit icall is
   * wrapped by name (rd_icall.h). */
  if (p && !strcmp(symbol, "il2cpp_add_internal_call")) {
    static const rd_icall_override *tables[3];
    tables[0] = nx_input_icall_table();
    tables[1] = rd_smoke_icall_table();
    tables[2] = rd_menu_icall_table();
    rd_icall_setup(tables, 3, (rd_add_icall_fn)p);
    debugPrintf("[input] dlsym(il2cpp_add_internal_call) -> icall override wrapper\n");
    return (void *)rd_add_internal_call;
  }
  /* The engine's collections (UnloadUnusedAssets on scene loads) run although the loader
   * keeps the GC off in play (rd_gcload.h). */
  if (p && !strcmp(symbol, "il2cpp_gc_collect")) {
    debugPrintf("[gc] dlsym(il2cpp_gc_collect) -> collect with the GC on for it\n");
    return rd_gcload_wrap(p);
  }
  if (p) return p;
  /* 2) one of our libc/GLES/EGL shims (the engine dlopen()s libGLESv2.so etc.
   *    and dlsym()s glGetString/glGetIntegerv, which are shims, not exports) */
  uintptr_t shim = dynlib_find_export(symbol);
  if (shim && !strncmp(symbol, "gl", 2)) shim = (uintptr_t)egl_wrap_gl(symbol, (void *)shim);
  if (shim) { debugPrintf("dlsym(%s) -> %p [shim]\n", symbol, (void *)shim); return (void *)shim; }
  /* 3) the full GLES/EGL API (~150 entry points) lives in mesa, beyond our
   *    static table -- resolve any gl or egl symbol via eglGetProcAddress. */
  if (!strncmp(symbol, "gl", 2) || !strncmp(symbol, "egl", 3)) {
    p = egl_GetProcAddress_timed(symbol);
    if (p) { debugPrintf("dlsym(%s) -> %p [egl]\n", symbol, p); return p; }
  }
  debugPrintf("dlsym(%s) -> NULL\n", symbol);
  return NULL;
}
