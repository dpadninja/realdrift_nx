/* rd_quality.c -- see rd_quality.h. MIT (see LICENSE). */
#include <stdint.h>
#include "rd_quality.h"
#include "rd_a64.h"
#include "rd_il2cpp.h"
#include "rd_patchsites.h"
#include "util.h"

#define ICALL_GET_AA "UnityEngine.QualitySettings::get_antiAliasing"
#define GETTER_WORDS 9   /* the getter's length in 5.0.7 and 5.0.8 */

static int g_state;                 /* 0 = not tried, 1 = located, -1 = not available */
static rd_quality_aa g_aa;
static void *(*g_get_settings)(void);

/* Locates the field once. 1 = ready, -1 = not available. */
static int locate(void) {
  if (g_state) return g_state;
  g_state = -1;
  const uint32_t *code = g_il2cpp.resolve_icall ? g_il2cpp.resolve_icall(ICALL_GET_AA) : NULL;
  const int r = code ? rd_find_quality_aa(code, GETTER_WORDS, &g_aa) : RD_SITE_NOT_FOUND;
  if (r != RD_SITE_OK) {
    debugPrintf("[quality] %s: not located (result %d)\n", ICALL_GET_AA, r);
    return g_state;
  }
  const uint64_t at = (uint64_t)(uintptr_t)(code + g_aa.call);
  g_get_settings = (void *(*)(void))(uintptr_t)rd_a64_branch_target(at, code[g_aa.call]);
  g_state = 1;
  return g_state;
}

int rd_quality_set_aa(int samples) {
  if (locate() < 0) return -1;
  char *mgr = g_get_settings();
  char *levels = mgr ? *(char **)(mgr + g_aa.levels) : NULL;
  if (!levels) return -1;
  const int32_t cur = *(int32_t *)(mgr + g_aa.current);
  int32_t *aa = (int32_t *)(levels + (int64_t)cur * g_aa.stride + g_aa.field);
  const int was = *aa;
  if (was != samples) {
    *aa = samples;
    debugPrintf("[quality] level %d antiAliasing %d -> %d\n", (int)cur, was, samples);
  }
  return was;
}
