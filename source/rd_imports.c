/* rd_imports.c -- Real Drift supplementary import table. MIT (see LICENSE).
 *
 * Symbols imported by Real Drift's arm64 libunity.so / libil2cpp.so (Unity 2018.4.14f1)
 * that the inherited badpiggies_nx tables do not provide. Diff produced by comparing
 * analysis/all.imports.txt against the inherited tables -- see
 * analysis/research/03-native-imports.md. Appended to the combined table in imports.c.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include <wctype.h>
#include "so_util.h"
#include "libc_shim.h"
#include "rd_iov.h"

static long rd_stub0(void) { return 0; }

static long rd_writev(int fd, const rd_iovec *iov, int iovcnt) {
  return rd_writev_via((rd_write_fn)write_fake, fd, iov, iovcnt);
}

DynLibFunction rd_dynlib_functions[] = {
  { "atof",      (uintptr_t)&atof },
  { "frexp",     (uintptr_t)&frexp },
  { "remainder", (uintptr_t)&remainder },
  { "strsep",    (uintptr_t)&strsep },
  { "writev",    (uintptr_t)&rd_writev },
  { "iswctype",  (uintptr_t)&iswctype },
  { "wctype",    (uintptr_t)&wctype },
  { "wcsftime",  (uintptr_t)&wcsftime },
  /* rwlocks are lazily heap-allocated by libc_shim.c; leaking one on destroy is harmless. */
  { "pthread_rwlock_destroy", (uintptr_t)&rd_stub0 },
  /* Android "potentially blocking region" markers: debug-only annotations, no-ops. */
  { "__google_potentially_blocking_region_begin", (uintptr_t)&rd_stub0 },
  { "__google_potentially_blocking_region_end",   (uintptr_t)&rd_stub0 },
};
int rd_dynlib_numfunctions = (int)(sizeof(rd_dynlib_functions) / sizeof(rd_dynlib_functions[0]));
