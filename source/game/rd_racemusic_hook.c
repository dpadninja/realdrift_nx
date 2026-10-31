/* rd_racemusic_hook.c -- see rd_racemusic.h. MIT (see LICENSE). */
#include <stdint.h>
#include "rd_racemusic.h"
#include "rd_a64.h"
#include "rd_game_symbols.h"
#include "rd_il2cpp.h"
#include "rd_patchsites.h"
#include "rd_platform.h"
#include "so_util.h"
#include "util.h"

#define SCAN_WORDS 256   /* PlayMusic is 185 words in 5.0.7 and 5.0.8 */

typedef void  (*fn_awake)(void *self, const void *mi);
typedef void *(*fn_load)(void *path, const void *mi);
typedef void  (*fn_set_flags)(void *self, int32_t flags, const void *mi);

static int g_done;
static fn_awake g_orig_awake;
static fn_load g_load;
static const void *g_load_mi;
static fn_set_flags g_set_flags;
static const void *g_set_flags_mi;
static uint32_t g_musics_off;
static rd_music_cache g_cache;

/* A method by class, name and argument count, or NULL (the MethodInfo; word 0 is its code). */
static void **method(void *klass, const char *name, int argc) {
  return klass && g_il2cpp.class_get_method_from_name
             ? g_il2cpp.class_get_method_from_name(klass, name, argc) : NULL;
}

/* An ASCII copy of a managed string into buf (cap bytes); 0 if it does not fit or is not
 * ASCII (then it is not cached). */
static int ascii(void *str, char *buf, int cap) {
  const int32_t n = str ? g_il2cpp.string_length(str) : -1;
  if (n <= 0 || n >= cap) return 0;
  const uint16_t *c = g_il2cpp.string_chars(str);
  for (int32_t i = 0; i < n; i++) {
    if (c[i] == 0 || c[i] > 0x7f) return 0;
    buf[i] = (char)c[i];
  }
  buf[n] = 0;
  return 1;
}

/* RaceAudioManager.Awake, inside the race scene's load frame: the engine's Awake, then every
 * track of backgroundMusics loaded and held. */
static void awake(void *self, const void *mi) {
  g_orig_awake(self, mi);
  void *names = self ? *(void **)((char *)self + g_musics_off) : NULL;
  const uint32_t n = names ? g_il2cpp.array_length(names) : 0;
  const uint64_t t0 = rd_now_ns();
  int loaded = 0, failed = 0;
  for (uint32_t i = 0; i < n; i++) {
    void *path = rd_il2cpp_array_at(names, i);
    char name[RD_MUSIC_NAME_MAX];
    if (!ascii(path, name, sizeof name) || rd_music_cache_add(&g_cache, name) != 1) continue;
    void *clip = g_load(path, g_load_mi);
    /* the flag keeps the clip through UnloadUnusedAssets on scene loads (the engine's asset
     * liveness need not count a GC handle); the handle keeps its managed object alive */
    if (clip) g_set_flags(clip, RD_HIDE_DONT_UNLOAD, g_set_flags_mi);
    if (clip && g_il2cpp.gchandle_new(clip, 0)) loaded++;
    else failed++;
  }
  if (loaded || failed)
    debugPrintf("[music] %d of %u tracks loaded with the race and kept (%d held), %d failed, "
                "in %llu ms\n", loaded, (unsigned)n, g_cache.n, failed,
                (unsigned long long)((rd_now_ns() - t0) / RD_NS_PER_MS));
}

/* Part 1: the UnloadUnusedAssets call in PlayMusic becomes a nop. */
static void patch_play_music(void *audio, void *res) {
  void **play_mi = method(audio, RD_MTH_PLAY_MUSIC, 0);
  void **unload_mi = method(res, RD_MTH_UNLOAD_UNUSED, 0);
  const uint32_t *play = play_mi ? (const uint32_t *)play_mi[0] : NULL;
  const uint32_t *unload = unload_mi ? (const uint32_t *)unload_mi[0] : NULL;
  size_t at = 0;
  const int r = play && unload
                    ? rd_find_call(play, SCAN_WORDS, (uint64_t)(uintptr_t)play,
                                   (uint64_t)(uintptr_t)unload, &at)
                    : RD_SITE_NOT_FOUND;
  const uint32_t nop = RD_A64_NOP;
  if (r != RD_SITE_OK || so_patch_code((void *)(uintptr_t)(play + at), &nop, sizeof nop)) {
    debugPrintf("[music] %s.%s: not patched (result %d)\n", RD_CLS_RACE_AUDIO, RD_MTH_PLAY_MUSIC, r);
    return;
  }
  debugPrintf("[music] %s.%s: no %s on a track change (word %zu)\n", RD_CLS_RACE_AUDIO,
              RD_MTH_PLAY_MUSIC, RD_MTH_UNLOAD_UNUSED, at);
}

/* Part 2: Awake wrapped to load and keep every track. */
static void wrap_awake(void *audio, void *res) {
  void **awake_mi = method(audio, RD_MTH_RACE_AUDIO_AWAKE, 0);
  void **load_mi = method(res, RD_MTH_RES_LOAD, 1);
  void *object = rd_il2cpp_class(&g_il2cpp, RD_ASM_UNITY_CORE, RD_NS_UNITY, RD_CLS_OBJECT);
  void **flags_mi = method(object, RD_MTH_SET_HIDE_FLAGS, 1);
  if (!awake_mi || !awake_mi[0] || !load_mi || !load_mi[0] || !flags_mi || !flags_mi[0] ||
      !g_il2cpp.array_length ||
      !g_il2cpp.gchandle_new || !g_il2cpp.string_length || !g_il2cpp.string_chars ||
      !rd_il2cpp_field_offset(&g_il2cpp, audio, RD_FLD_BG_MUSICS, &g_musics_off)) {
    debugPrintf("[music] %s.%s / %s.%s / %s not found: tracks load in the race\n",
                RD_CLS_RACE_AUDIO, RD_MTH_RACE_AUDIO_AWAKE, RD_CLS_RESOURCES, RD_MTH_RES_LOAD,
                RD_FLD_BG_MUSICS);
    return;
  }
  g_set_flags = (fn_set_flags)flags_mi[0];
  g_set_flags_mi = flags_mi;
  g_load = (fn_load)load_mi[0];
  g_load_mi = load_mi;
  g_orig_awake = (fn_awake)awake_mi[0];
  awake_mi[0] = (void *)awake;
  debugPrintf("[music] %s.%s hooked: every track loaded with the race\n", RD_CLS_RACE_AUDIO,
              RD_MTH_RACE_AUDIO_AWAKE);
}

int rd_racemusic_try_install(void) {
  if (g_done) return 1;
  void *audio = rd_il2cpp_class(&g_il2cpp, RD_ASM_GAME, RD_NS_GAME, RD_CLS_RACE_AUDIO);
  if (!audio) return 0;                                        /* not loaded yet */
  g_done = 1;
  void *res = rd_il2cpp_class(&g_il2cpp, RD_ASM_UNITY_CORE, RD_NS_UNITY, RD_CLS_RESOURCES);
  patch_play_music(audio, res);
  wrap_awake(audio, res);
  return 1;
}
