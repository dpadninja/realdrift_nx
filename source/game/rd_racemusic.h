/* rd_racemusic.h -- no Resources.UnloadUnusedAssets when the race music changes track.
 *
 * RaceAudioManager.Update calls PlayMusic whenever the background music is not playing (a
 * track ended, or the music was paused), and PlayMusic loads the next clip and then calls
 * Resources.UnloadUnusedAssets: the engine stops every thread to find unreferenced assets,
 * and with garbage collection on its request (rd_gcload.h) also collects -- a hitch in the
 * middle of a race, at every track change (it already stuttered there before). The call's
 * result is unused; it becomes a `nop`. The previous clip stays loaded until the next scene
 * load, which unloads unused assets anyway (a few MB).
 *
 * Both methods are found by name through il2cpp, the call by its target (rd_find_call: the
 * only `bl UnloadUnusedAssets` in PlayMusic, word 103 in 5.0.7 and 5.0.8). Main thread.
 * MIT (see LICENSE). */
#ifndef RD_RACEMUSIC_H
#define RD_RACEMUSIC_H

/* Every RD_DRIVE_RETRY_FRAMES until Assembly-CSharp is loaded, then once: patches the call.
 * Returns 1 when done (patched or given up, logged), 0 to retry. */
int rd_racemusic_try_install(void);

#endif
