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

/* libil2cpp: Boehm GC stop-the-world globals, from GC_suspend_handler_inner, the suspend
 * signal wrapper that calls it, and the restart signal handler. */
int rd_find_gc_vars(const rd_image *im, rd_gc_vars *out);

#endif
