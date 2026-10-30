/* rd_sleek_hook.c -- see rd_sleek.h. MIT (see LICENSE).
 *
 * The clear: the method is found by name through il2cpp; its code is checked against the
 * expected shape (rd_find_skip_clear: frame, `bl` set_active, the clear, the epilogue) and one
 * word -- the one after the set_active call -- becomes `b <epilogue>`.
 * Bloom / vignette: FindObjectsOfType(SleekRenderPostProcess) between engine frames (the
 * component lives on PersistentData, so it is found in menus and races), then the two bools of
 * its settings asset. Main thread. */
#include <stdint.h>
#include "rd_sleek.h"
#include "rd_a64.h"
#include "rd_game_symbols.h"
#include "rd_il2cpp.h"
#include "rd_patchsites.h"
#include "rd_platform.h"
#include "config.h"
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

#define FIELD(obj, off, type) (*(type *)((char *)(obj) + (off)))

static int g_fx_state;                           /* 0 = not tried, 1 = ready, -1 = off */
static void *(*g_find_objects)(void *type);
static void *g_post_type;                        /* System.Type of SleekRenderPostProcess */
static uint32_t g_settings_off, g_bloom_off, g_vignette_off;

/* Resolves the names once Assembly-CSharp-firstpass is loaded.
 * 1 = ready, 0 = not yet, -1 = off. */
static int fx_install(void) {
  void *post = rd_il2cpp_class(&g_il2cpp, RD_ASM_FIRSTPASS, RD_NS_SLEEK, RD_CLS_SLEEK_POST);
  if (!post) return 0;
  void *settings =
      rd_il2cpp_class(&g_il2cpp, RD_ASM_FIRSTPASS, RD_NS_SLEEK, RD_CLS_SLEEK_SETTINGS);
  g_find_objects = g_il2cpp.resolve_icall(RD_ICALL_FIND_OBJECTS);
  g_post_type = g_il2cpp.type_get_object(g_il2cpp.class_get_type(post));
  if (!settings || !g_find_objects || !g_post_type ||
      !rd_il2cpp_field_offset(&g_il2cpp, post, RD_FLD_SLEEK_SETTINGS, &g_settings_off) ||
      !rd_il2cpp_field_offset(&g_il2cpp, settings, RD_FLD_SLEEK_BLOOM, &g_bloom_off) ||
      !rd_il2cpp_field_offset(&g_il2cpp, settings, RD_FLD_SLEEK_VIGNETTE, &g_vignette_off)) {
    debugPrintf("[sleek] settings names not found: ge_bloom / ge_vignette off\n");
    return -1;
  }
  return 1;
}

void rd_sleek_frame(void) {
  if (g_fx_state < 0 || (port_frame_count() % RD_SLEEK_FRAMES)) return;
  if (config.ge_bloom && config.ge_vignette) {
    g_fx_state = -1;
    return;
  }
  if (!g_fx_state && (g_fx_state = fx_install()) <= 0) return;
  void *arr = g_find_objects(g_post_type);
  const uint32_t n = arr ? g_il2cpp.array_length(arr) : 0;
  for (uint32_t i = 0; i < n; i++) {
    void *s = FIELD(rd_il2cpp_array_at(arr, i), g_settings_off, void *);
    if (!s) continue;
    const int b = rd_sleek_set_flag(&FIELD(s, g_bloom_off, uint8_t), config.ge_bloom);
    const int v = rd_sleek_set_flag(&FIELD(s, g_vignette_off, uint8_t), config.ge_vignette);
    if (b || v)
      debugPrintf("[sleek] Graphics Enhancements: bloom %d, vignette %d\n", config.ge_bloom,
                  config.ge_vignette);
  }
}
