/* rd_sleek_hook.c -- see rd_sleek.h. MIT (see LICENSE).
 *
 * The method is found by name through il2cpp; its code is checked against the expected shape
 * (rd_find_skip_clear: frame, `bl` set_active, the clear, the epilogue) and one word -- the one
 * after the set_active call -- becomes `b <epilogue>`. Main thread. */
#include <stdint.h>
#include "rd_sleek.h"
#include "rd_a64.h"
#include "rd_game_symbols.h"
#include "rd_il2cpp.h"
#include "rd_patchsites.h"
#include "so_util.h"
#include "util.h"

#define SCAN_WORDS 64   /* the method is 23 words in 5.0.8; its epilogue must be within this */

static int g_done;

int rd_sleek_try_install(void) {
  if (g_done) return 1;
  void *klass = rd_il2cpp_class(&g_il2cpp, RD_ASM_FIRSTPASS, RD_NS_SLEEK, RD_CLS_SLEEK_POST);
  if (!klass) return 0;                                        /* not loaded yet */
  g_done = 1;
  void **m = g_il2cpp.class_get_method_from_name ? g_il2cpp.class_get_method_from_name(klass, RD_MTH_SLEEK_SET_CLEAR, 1)
                                                 : NULL;
  const uint32_t *code = m ? (const uint32_t *)m[0] : NULL;
  size_t from = 0, to = 0;
  const int r = code ? rd_find_skip_clear(code, SCAN_WORDS, &from, &to) : RD_SITE_NOT_FOUND;
  if (r != RD_SITE_OK) {
    debugPrintf("[sleek] %s.%s: not patched (result %d)\n", RD_CLS_SLEEK_POST, RD_MTH_SLEEK_SET_CLEAR, r);
    return 1;
  }
  const uint64_t at = (uint64_t)(uintptr_t)(code + from), epilogue = (uint64_t)(uintptr_t)(code + to);
  const uint32_t b = rd_a64_b(at, epilogue);
  if (!b || so_patch_code((void *)(uintptr_t)at, &b, sizeof b)) {
    debugPrintf("[sleek] %s: patch failed\n", RD_MTH_SLEEK_SET_CLEAR);
    return 1;
  }
  debugPrintf("[sleek] %s: the clear is skipped (every pass overwrites every pixel)\n", RD_MTH_SLEEK_SET_CLEAR);
  return 1;
}
