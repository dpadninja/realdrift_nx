/* rd_racemusic_hook.c -- see rd_racemusic.h. MIT (see LICENSE). */
#include <stdint.h>
#include "rd_racemusic.h"
#include "rd_a64.h"
#include "rd_game_symbols.h"
#include "rd_il2cpp.h"
#include "rd_patchsites.h"
#include "so_util.h"
#include "util.h"

#define SCAN_WORDS 256   /* PlayMusic is 185 words in 5.0.7 and 5.0.8 */

static int g_done;

/* A method's code by class and name (0 arguments), or NULL. */
static const uint32_t *method_code(void *klass, const char *name) {
  void **m = klass && g_il2cpp.class_get_method_from_name
                 ? g_il2cpp.class_get_method_from_name(klass, name, 0) : NULL;
  return m ? (const uint32_t *)m[0] : NULL;
}

int rd_racemusic_try_install(void) {
  if (g_done) return 1;
  void *audio = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_RACE_AUDIO);
  if (!audio) return 0;                                        /* not loaded yet */
  g_done = 1;
  void *res = rd_il2cpp_class(&g_il2cpp, RD_ASM_UNITY_CORE, RD_NS_UNITY, RD_CLS_RESOURCES);
  const uint32_t *play = method_code(audio, RD_MTH_PLAY_MUSIC);
  const uint32_t *unload = method_code(res, RD_MTH_UNLOAD_UNUSED);
  size_t at = 0;
  const int r = play && unload
                    ? rd_find_call(play, SCAN_WORDS, (uint64_t)(uintptr_t)play,
                                   (uint64_t)(uintptr_t)unload, &at)
                    : RD_SITE_NOT_FOUND;
  const uint32_t nop = RD_A64_NOP;
  if (r != RD_SITE_OK || so_patch_code((void *)(uintptr_t)(play + at), &nop, sizeof nop)) {
    debugPrintf("[music] %s.%s: not patched (result %d)\n", RD_CLS_RACE_AUDIO, RD_MTH_PLAY_MUSIC, r);
    return 1;
  }
  debugPrintf("[music] %s.%s: no %s on a track change (word %zu)\n", RD_CLS_RACE_AUDIO,
              RD_MTH_PLAY_MUSIC, RD_MTH_UNLOAD_UNUSED, at);
  return 1;
}
