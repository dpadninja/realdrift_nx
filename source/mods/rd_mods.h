/* rd_mods.h -- the mod host: loads the mods in <game folder>/mods/ and runs them.
 *
 * At boot (before Unity starts), rd_mods_boot reads every mods/<id>/mod.ini, orders the mods
 * (rd_modhost.h), registers their files/apk and files/obb files with rd_vfile (the archives
 * Unity then reads contain them), collects patch.ini values, loads mod.so and calls its
 * rd_mod_entry with the rd_mod_api table (sdk/rd_mod.h, filled in rd_mod_api.c). Every frame,
 * rd_mods_frame detects when the game is ready and when the scene changes, applies patch.ini
 * values to live objects and calls the mods' callbacks. Main thread only.
 * Problems are logged with RD_LOG_TAG_MODS and skip the mod, never the game.
 * MIT (see LICENSE). */
#ifndef RD_MODS_H
#define RD_MODS_H

#include <stdint.h>

/* Loads the mods of `home` (the game folder) unless config.txt has mods = 0. Call after the
 * game libraries are loaded and before Unity starts (it opens the archives). */
void rd_mods_boot(const char *home);

/* Per-frame work, from the main loop before nativeRender. */
void rd_mods_frame(uint32_t frame);

#endif
