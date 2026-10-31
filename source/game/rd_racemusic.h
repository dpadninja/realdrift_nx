/* rd_racemusic.h -- the race music without hitches.
 *
 * 1. No Resources.UnloadUnusedAssets on a track change. RaceAudioManager.Update calls
 *    PlayMusic whenever the background music is not playing (a track ended, or the music was
 *    paused), and PlayMusic loads the next clip and then calls Resources.UnloadUnusedAssets:
 *    the engine stops every thread to find unreferenced assets, and with garbage collection
 *    on its request (rd_gcload.h) also collects -- a hitch in the middle of a race, at every
 *    track change. The call's result is unused; it becomes a `nop`. Both methods are found by
 *    name through il2cpp, the call by its target (rd_find_call: the only `bl
 *    UnloadUnusedAssets` in PlayMusic, word 103 in 5.0.7 and 5.0.8).
 * 2. Every track loaded while the race loads. PlayMusic's Resources.Load(backgroundMusics[i])
 *    read ~2 MB from the OBB and set the clip up in the race (a 65 ms frame at a track
 *    change). RaceAudioManager.Awake -- run by the engine inside the scene's load frame,
 *    through MethodInfo.methodPointer, which is swapped -- now loads every name of
 *    backgroundMusics once and keeps each clip -- HideFlags.DontUnloadUnusedAsset, so scene
 *    loads do not unload it, and a GC handle for its managed object -- and PlayMusic's Load
 *    finds it in memory. Nothing is released: the tracks stay loaded for the session (~19 MB
 *    of .resource data in the OBB).
 *
 * The name set is pure (tests/c/test_rd_racemusic.c); rd_racemusic_hook.c is the glue.
 * Main thread. MIT (see LICENSE). */
#ifndef RD_RACEMUSIC_H
#define RD_RACEMUSIC_H

#define RD_MUSIC_MAX       32   /* tracks remembered (the game has 9) */
#define RD_MUSIC_NAME_MAX  96   /* bytes per Resources path, NUL included */

/* The tracks loaded and held so far. Zero-initialised = empty. */
typedef struct {
  int  n;
  char names[RD_MUSIC_MAX][RD_MUSIC_NAME_MAX];
} rd_music_cache;

/* Adds `name` to the set: 1 if new (load and hold it), 0 if already held, -1 if it cannot be
 * held (empty, too long, set full). */
int rd_music_cache_add(rd_music_cache *c, const char *name);

/* Every RD_DRIVE_RETRY_FRAMES until Assembly-CSharp is loaded, then once: patches the
 * UnloadUnusedAssets call and wraps Awake. Returns 1 when done (logged), 0 to retry. */
int rd_racemusic_try_install(void);

#endif
